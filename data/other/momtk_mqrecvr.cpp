#include <momtk_mqrecvr.h>

#include <sysutil_ident.h>
SYSUTIL_IDENT_RCSID(momtk_mqrecvr_cpp, "$Id$ $CSID$")
#include <actionsvclient_client.h>
#include <bael_log.h>
#include <bsl_string.h>
#include <bslmt_lockguard.h>
#include <ctrace.h>
#include <flowbotclient_client.h>
#include <gutz_metrics.h>
#include <iomanip>
#include <iostream>
#include <momdb_comdb2conn.h>
#include <momsrvr_aqilistener.h>
#include <momsrvr_processor.h>
#include <momsrvr_pxlist.h>
#include <momsrvr_utils.h>
#include <momtk_DbInterface.h>
#include <momtk_remoteupd.h>
#include <momtk_tktimpl.h>
#include <sstream>
#include <stdlib.h>
#include <strings.h>
#include <tssrbigutil_pnumutil.h>

#include <bbit/201804/bbit_enable_momsrvr_backout_queue.h> // BREG 316636
#include <bbit/201902/bbit_process_rabbitmq_tradeworkfloweventsconsumer.h> // BREG 345667

extern "C" {
#include <logger.h>
#include <tspodapi.h>
#include <unistd.h>

int machine_();
bbint32_t ts_beta_mach(const short mach);
void* threadStarter(void* env);
}

namespace {
const string GUTS_NAMESPACE_ROOT = "momsrvr";
const string GUTS_NAMESPACE_MOMSRVR = "ibmmqlistener";
const string GUTS_METRIC_MOMSRVR_FUNCTION = GUTS_NAMESPACE_MOMSRVR + ".function";
const string GUTS_METRIC_MOMSRVR_MUTEX = GUTS_NAMESPACE_MOMSRVR + ".mutex";
const string GUTS_TAG_FUNCTION_NAME = "functionName";
const string GUTS_TAG_PRICING_NUMBER = "pricingNumber";
} // namespace

using namespace BloombergLP;

void* threadStarter(void* env)
{
    MQListener* me = static_cast<MQListener*>(env);
    me->run();
    return NULL;
}

MQListener::MQListener()
    : m_done(false)
    , m_connected(false)
    , m_openedQueues(false)
    , m_shouldReopen(false)
    , m_enableBackoutQueue(bbit_enable_momsrvr_backout_queue__value())
    , m_retryCount(0)
    , m_emptyPxCount(0)
{
    BAEL_LOG_SET_CATEGORY("M_MOMSRVR.MQLISTENER.MQLISTENER");

    BAEL_LOG_INFO << __func__ << ": Attempting to connect to IBM MQ listener..." << BAEL_LOG_END;

    // Sets interface for db and message processor
    if (!setProcessor()) {
        BAEL_LOG_ERROR
            << "Failed to set IBM MQ listener processor error_type=failed_setting_processor_ibm"
            << BAEL_LOG_END;
        throw MQListenerException("setProcessor error");
    }

    // pod MQ
    int machine_id = machine_();
    bsl::string qmName = momtktUpdater::getMomMQpodQM(machine_id); // get queue manager name
    bsl::string qName = momtktUpdater::getMomMQpodQname(machine_id); // get queue name
    BAEL_LOG_INFO << "QM: '" << qmName << "'  Q: '" << qName << "'" << BAEL_LOG_END;

    setMQEnvSAT();

    memset(&m_conn, 0, sizeof(btsmi_CONNECTION));
    memset(&m_queue, 0, sizeof(btsmi_QUEUE));
    memset(m_qName, 0, sizeof(m_qName));
    strncpy(m_qName, qName.c_str(), sizeof(m_qName) - 1);
    memset(m_qmName, 0, sizeof(m_qmName));
    strncpy(m_qmName, qmName.c_str(), sizeof(m_qmName) - 1);

    getSTKSize();

    BAEL_LOG_INFO << "Initializing MQ listener thread qName=" << qName << " qmName=" << qmName
                  << BAEL_LOG_END;

    int rcode = pthread_create(&m_listenerThread, NULL, &threadStarter, this);
    if (rcode != 0) {
        BAEL_LOG_ERROR << "Failed to start IBM MQ listener thread" << BAEL_LOG_END;
        throw MQListenerException("pthread_create error");
    }

    BAEL_LOG_INFO << __func__ << ": Initialized IBM MQ listener" << BAEL_LOG_END;
}

MQListener::~MQListener()
{
    BAEL_LOG_SET_CATEGORY("M_MOMSRVR.MQLISTENER.~MQLISTENER");

    m_done = true;

    BAEL_LOG_INFO << "Waiting for MQ listener thread to exit..." << BAEL_LOG_END;

    int rcode = pthread_join(m_listenerThread, NULL);
    if (rcode != 0) {
        BAEL_LOG_ERROR << "pthread_join failed on MQ listener thread" << BAEL_LOG_END;
    }

    BAEL_LOG_INFO << __func__ << ": Terminated MQ listener" << BAEL_LOG_END;
}

bool MQListener::openListenQueue(char* qName, char* qmName)
{
    BAEL_LOG_SET_CATEGORY("M_MOMSRVR.MQLISTENER.OPENLISTENQUEUE");
    btsmi_LONG reason = 0;
    btsmi_LONG compCode = btsmi_OK;

    ostringstream SelectionString;
    int numPx = getSelectionString(SelectionString);
    BAEL_LOG_INFO << "Attempting to open queue. "
                  << " numPx=" << numPx << " selectionstring=" << SelectionString.str()
                  << BAEL_LOG_END;
    /*
      MQOO_INPUT_SHARED: Allows us to call btsmi_Get
      MQOO_INQUIRE: Allows us to call MQINQ
      MQOO_SAVE_ALL_CONTEXT: Saves user identificatier in message
    */
    m_queue.options |= MQOO_INPUT_SHARED | MQOO_INQUIRE | MQOO_SAVE_ALL_CONTEXT;
    compCode = btsmi_SelectOpen(&m_conn, &m_queue, qName, &reason,
                                (char*)SelectionString.str().c_str());

    if (compCode != btsmi_OK) {
        BAEL_LOG_ERROR << "btsmi_SelectOpen failed."
                       << " qName=" << qName << " qmName=" << qmName << " compCode=" << compCode
                       << " reason=" << reason << " error_type=failed_initialize_queue_ibm"
                       << BAEL_LOG_END;
        return false;
    }

    BAEL_LOG_INFO << "Successfully connected to MQ."
                  << " qName=" << qName << " qmName=" << qmName << BAEL_LOG_END;
    return true;
}

bool MQListener::openSendQueue(char* qName, char* qmName)
{
    BAEL_LOG_SET_CATEGORY("M_MOMSRVR.MQLISTENER.OPENSENDQUEUE");
    btsmi_LONG reason = 0;
    btsmi_LONG compCode = btsmi_OK;

    btsmi_OpenCode openCode;
    /*
      btsmi_OUTPUT: Allows us to call btsmi_put
      MQOO_SET_IDENTITY_CONTEXT: Allows us to set user identifier in message
    */
    openCode = btsmi_OUTPUT | MQOO_SET_IDENTITY_CONTEXT;

    compCode = btsmi_Open(&m_conn, &m_backoutQueue, qName, openCode, &reason);

    if (compCode != btsmi_OK) {
        BAEL_LOG_ERROR << "btsmi_Open failed."
                       << " qName=" << qName << " qmName=" << qmName << " compCode=" << compCode
                       << " reason=" << reason << BAEL_LOG_END;
        return false;
    }

    BAEL_LOG_INFO << "Successfully connected to MQ."
                  << " qName=" << qName << " qmName=" << qmName << BAEL_LOG_END;
    return true;
}

bool MQListener::checkConnection()
{
    BAEL_LOG_SET_CATEGORY("M_MOMSRVR.MQLISTENER.CHECKCONNECTION");

    // Try to connect to queue manager
    if (!m_connected && !connect()) {
        BAEL_LOG_ERROR << "Unable to connect to queue manager. qm=" << m_qmName << BAEL_LOG_END;
        return false;
    }

    // Try to connect to queues
    {
        bslmt::LockGuard<bslmt::Mutex> lguard(&momsrvr::PxList::updatedPxListMutex);
        GUTZ_METRICS_TAGGED_DISTRIBUTION_TIMER_MS(GUTS_NAMESPACE_ROOT.c_str(),
                                                  GUTS_METRIC_MOMSRVR_MUTEX.c_str(),
                                                  GUTS_TAG_FUNCTION_NAME.c_str(), "updatedPxList");
        if (momsrvr::PxList::isPxListUpdated() && m_openedQueues) {
            closeQueues();
            momsrvr::PxList::updatedPxListCaptured();
        }

        if (momsrvr::PxList::isEmpty()) {
            if (m_emptyPxCount < 10) {
                BAEL_LOG_INFO << "Sleeping due to empty px list." << BAEL_LOG_END;
            }
            m_emptyPxCount++;
            return false;
        } else {
            m_emptyPxCount = 0;
        }

        if (!m_openedQueues && !openQueues()) {
            BAEL_LOG_ERROR << "Unable to open queues." << BAEL_LOG_END;
            return false;
        }
    }

    return true;
}

void MQListener::run()
{
    BAEL_LOG_SET_CATEGORY("M_MOMSRVR.MQLISTENER.RUN")

    btsmi_LONG reason = 0;
    btsmi_LONG compCode = btsmi_OK;
    btsmi_GET_MESSAGE m;
    char buffer[MAX_MQ_MESSAGE_SIZE + 1];
    btsmi_InitGetMessage(&m, buffer, sizeof(buffer));
    btsmi_GetMessage_SetWaitInterval(&m, TIMEOUT_INTERVAL);

    // single GET transaction
    btsmi_GetMessage_SetSyncPoint(&m, 1);

    BAEL_LOG_INFO << "Begin MQ polling." << BAEL_LOG_END;

    while (!m_done) {
        if (!checkConnection()) {
            // Something is wrong with the connection. Retry after sleeping
            sleep(30);
            continue;
        }

        try {
            m.numread = 0;
            BAEL_LOG_DEBUG << "Attempting to get message from queue." << BAEL_LOG_END;
            {
                GUTZ_METRICS_TAGGED_DISTRIBUTION_TIMER_MS(
                    GUTS_NAMESPACE_ROOT.c_str(), GUTS_METRIC_MOMSRVR_FUNCTION.c_str(),
                    GUTS_TAG_FUNCTION_NAME.c_str(), "btsmi_Get");
                compCode = btsmi_Get(&m_queue, &m, &reason);
            }

            BAEL_LOG_DEBUG << "Called btsmi_Get."
                           << " compCode=" << compCode << " reason=" << reason << BAEL_LOG_END;

            if (compCode != btsmi_FAILED) {
                BAEL_LOG_DEBUG << "compCode is not btsmi_FAILED. Attempting to "
                                  "handle message."
                               << BAEL_LOG_END;

                try {
                    if (!m_enableBackoutQueue || m.desc.BackoutCount < m_backoutThreshold) {
                        // if we got something
                        handle((char*)(m.buf), m.numread);
                        BAEL_LOG_DEBUG << "handle is done." << BAEL_LOG_END;
                    } else {
                        BAEL_LOG_ERROR << "Backout count exceeded threshold. "
                                          "Putting message in backout queue."
                                       << " m.buf=" << m.buf << BAEL_LOG_END;

                        btsmi_PUT_MESSAGE backoutMessage;
                        btsmi_InitPutMessage(&backoutMessage, (char*)(m.buf), m.numread);
                        btsmi_PutMessage_SetExpiry(&backoutMessage, btsmi_EXPIRY_UNLIMITED);
                        btsmi_PutMessage_SetUserIdentifier(&backoutMessage, m.desc.UserIdentifier);
                        backoutMessage.options.Options |= MQPMO_SET_IDENTITY_CONTEXT;

                        compCode = btsmi_Put(&m_backoutQueue, &backoutMessage, &reason);

                        if (compCode != btsmi_OK) {
                            BAEL_LOG_ERROR << "Unable to put message in backout queue."
                                           << " m.buf=" << m.buf << BAEL_LOG_END;
                        }
                    }

                } catch (...) {
                    BAEL_LOG_ERROR << "Exception caught while handling message."
                                   << " m.buf=" << m.buf << " error_type=exception_ibm"
                                   << BAEL_LOG_END;

                    // try up to 10 times to rollback and try to process the
                    // message again after 10 times, commit the transaction so
                    // we can skip the message
                    m_retryCount++;
                    if (m_retryCount >= MAX_MSG_RETRY) {
                        BAEL_LOG_ERROR << "Max retry reached. Committing transaction."
                                       << " m.buf=" << m.buf << BAEL_LOG_END;
                        if (btsmi_CommitTran(&m_conn, &reason) != btsmi_OK) {
                            BAEL_LOG_ERROR << "Failed to commit transaction."
                                           << " m.buf=" << m.buf << " reason=" << reason
                                           << BAEL_LOG_END;
                        } else {
                            BAEL_LOG_DEBUG << "Successfully committed transaction."
                                           << " m.buf=" << m.buf << " reason=" << reason
                                           << BAEL_LOG_END;
                        }

                        continue;
                    }

                    BAEL_LOG_DEBUG << "Attempting to rollback transaction." << BAEL_LOG_END;
                    if (btsmi_RollBackTran(&m_conn, &reason) != btsmi_OK) {
                        BAEL_LOG_ERROR << "Failed to roll back transaction."
                                       << " m.buf=" << m.buf << " reason=" << reason
                                       << BAEL_LOG_END;
                    } else {
                        BAEL_LOG_DEBUG << "Successfully rolled back transaction."
                                       << " m.buf=" << m.buf << " reason=" << reason
                                       << BAEL_LOG_END;
                    }

                    // sleep before trying again
                    sleep(5);

                    continue;
                }

                // since our get/handle passed, let's reset tryCount
                m_retryCount = 0;

                try {
                    BAEL_LOG_DEBUG << "Attempting to commit transaction" << BAEL_LOG_END;

                    compCode = btsmi_CommitTran(&m_conn, &reason);

                    BAEL_LOG_DEBUG << "Commit is done."
                                   << " compCode=" << compCode << BAEL_LOG_END;

                    if (compCode != btsmi_OK) {
                        BAEL_LOG_ERROR << "Failed to commit transaction."
                                       << " m.buf=" << m.buf << " reason=" << reason
                                       << BAEL_LOG_END;
                    }
                } catch (...) {
                    BAEL_LOG_ERROR << "Exception caught while committing "
                                      "transaction. Attempting rollback."
                                   << " m.buf=" << m.buf << " reason=" << reason << BAEL_LOG_END;

                    compCode = btsmi_RollBackTran(&m_conn, &reason);

                    if (compCode != btsmi_OK) {
                        BAEL_LOG_ERROR << "Failed to roll back transaction."
                                       << " m.buf=" << m.buf << " reason=" << reason
                                       << BAEL_LOG_END;

                    } else {
                        BAEL_LOG_DEBUG << "Successfully rolled back transaction."
                                       << " m.buf=" << m.buf << " reason=" << reason
                                       << BAEL_LOG_END;
                    }
                }
            } else {
                if (reason != MQRC_NO_MSG_AVAILABLE) {
                    BAEL_LOG_ERROR << "Error reading from MQ."
                                   << " reason=" << reason << BAEL_LOG_END;

                    // if smthing wrong with queue or connection , then
                    // reconnect ....
                    if (!reconnect()) {
                        pgmlog(_STDERR + _STDOUT, "MQListener::run(): CANNOT RECONNECT TO MQ");
                        BAEL_LOG_ERROR << "Unable to reconnect to MQ" << BAEL_LOG_END;
                    }
                }
            }
        } catch (const std::exception& ex) {
            BAEL_LOG_ERROR << "Caught exception while attempting to get message."
                           << " reason=" << reason << " exception=" << ex.what() << BAEL_LOG_END;
        } catch (...) {
            BAEL_LOG_ERROR << "Caught unknown exception while attempting to get message."
                           << " reason=" << reason << BAEL_LOG_END;
        }
    }

    BAEL_LOG_INFO << "Terminating MQ polling" << BAEL_LOG_END;
}

bool MQListener::reconnect()
{
    if (m_connected) {
        disconnect();
    }

    return connect();
}

bool MQListener::disconnect()
{
    BAEL_LOG_SET_CATEGORY("M_MOMSRVR.MQLISTENER.DISCONNECT");
    bool result = true;
    btsmi_LONG reason = 0;
    btsmi_LONG compCode = btsmi_OK;

    if (m_openedQueues) {
        closeQueues();
    }

    compCode = btsmi_Disconnect(&m_conn, &reason);
    if (compCode != btsmi_OK) {
        BAEL_LOG_ERROR << "Error disconnecting from QManager."
                       << " qName=" << m_qName << " qmName=" << m_qmName << " reason=" << reason
                       << BAEL_LOG_END;
    } else {
        result = false;

        BAEL_LOG_INFO << "Disconnected from QManager."
                      << " qName=" << m_qName << " qmName=" << m_qmName << BAEL_LOG_END;
    }
    m_connected = false;
    return result;
}

bool MQListener::connect()
{
    BAEL_LOG_SET_CATEGORY("M_MOMSRVR.MQLISTENER.CONNECT");

    // Don't need to reconnect
    if (m_connected) {
        return true;
    }

    // Attempt to create connection
    btsmi_LONG reason = 0;
    btsmi_LONG compCode = btsmi_OK;
    if (strlen(m_qmName)) {
        compCode = btsmi_Connect(&m_conn, m_qmName, &reason);
    } else {
        compCode = btsmi_Connect(&m_conn, 0, &reason);
    }

    // Unable to create connection
    if (compCode != btsmi_OK) {
        BAEL_LOG_ERROR << "Unable to create connection."
                       << " reason=" << reason << BAEL_LOG_END;
        return false;
    }

    BAEL_LOG_INFO << "Successfully created connection." << BAEL_LOG_END;
    m_connected = true;

    return true;
}

bool MQListener::openQueues()
{
    BAEL_LOG_SET_CATEGORY("M_MOMSRVR.MQLISTENER.OPENQUEUES");

    // Attempt to open queue
    if (!openListenQueue(m_qName, m_qmName)) {
        BAEL_LOG_ERROR << "Unable to open queue."
                       << " qName=" << m_qName << BAEL_LOG_END;
        // lets close the connection :
        disconnect();
        return false;
    }

    if (m_enableBackoutQueue) {
        // Retrieve backout queue parameters and open backout queue
        if (!initializeBackoutQueueParameters() || !openSendQueue(m_backoutQueueName, m_qmName)) {
            BAEL_LOG_ERROR << "Unable to connect to the backout queue. "
                              "Disabling backout queue."
                           << " qName=" << m_backoutQueueName << BAEL_LOG_END;
            m_enableBackoutQueue = false;
        }
    }

    m_openedQueues = true;
    return true;
}

bool MQListener::closeQueues()
{
    BAEL_LOG_SET_CATEGORY("M_MOMSRVR.MQLISTENER.CLOSEQUEUES");

    bool result = true;
    btsmi_LONG reason = 0;
    btsmi_LONG compCode = btsmi_OK;

    // Attempt to close queue
    compCode = btsmi_Close(&m_queue, &reason);
    if (compCode != btsmi_OK) {
        BAEL_LOG_ERROR << "Unable to close queue."
                       << " qName=" << m_qName << " reason=" << reason << BAEL_LOG_END;
        result = false;
    } else {
        BAEL_LOG_DEBUG << "Closed queue."
                       << " qName=" << m_qName << BAEL_LOG_END;
    }

    if (m_enableBackoutQueue) {
        // Attempt to close backout queue
        compCode = btsmi_Close(&m_backoutQueue, &reason);
        if (compCode != btsmi_OK) {
            BAEL_LOG_ERROR << "Unable to close queue."
                           << " backoutQueueName=" << m_backoutQueueName << " reason=" << reason
                           << BAEL_LOG_END;
            result = false;
        } else {
            BAEL_LOG_DEBUG << "Closed queue."
                           << " backoutQueueName=" << m_backoutQueueName << BAEL_LOG_END;
        }
    }

    m_openedQueues = false;
    return result;
}

void MQListener::handle(const char* msgBuf, btsmi_LONG msgBufSz)
{
    BAEL_LOG_SET_CATEGORY("M_MOMSRVR.MQLISTENER.HANDLE");

    BAEL_LOG_DEBUG << "Recieved message from QM."
                   << " qName=" << m_qName << " backoutQueueName=" << m_backoutQueueName
                   << " msgBuf=" << msgBuf << BAEL_LOG_END;

    momtkt tkt;
    tkt = tkt.toObject(msgBuf);

    if (bbit_process_rabbitmq_tradeworkfloweventsconsumer__value()
        && (tkt.tktaction == ADD_TKT || tkt.tktaction == XPT_ALLOC_UPD)) {
        BAEL_LOG_INFO << __func__ << ": This ticket will be processed through aqiListener"
                      << " tktnum=" << tkt.tktnum << BAEL_LOG_END;
    } else {
        {
            GUTZ_METRICS_TAGGED_DISTRIBUTION_TIMER_MS(
                GUTS_NAMESPACE_ROOT.c_str(), GUTS_METRIC_MOMSRVR_MUTEX.c_str(),
                GUTS_TAG_FUNCTION_NAME.c_str(), "callToMomsrvrBusinessProcessMutexLock",
                GUTS_TAG_PRICING_NUMBER.c_str(), bsl::to_string(tkt.prcnum));
            momsrvr::AQIListener::callToMomsrvrBusinessProcessMutex.lock();
        }

        try {
            if (!mproc->process(tkt)) {
                BAEL_LOG_ERROR << "Failed to process ticket in momsrvr process,"
                               << " tktnum=" << tkt.tktnum << " prcnum=" << tkt.prcnum
                               << " error_type=failed_process_ticket_ibm" << BAEL_LOG_END;
            }
        } catch (...) {
            // Unlock mutex to avoid deadlock
            // Any exceptions here are going to be handled at upper level
            BAEL_LOG_INFO << "Exception caught in process(). Unlocking the mutex for tktnum="
                          << tkt.tktnum << " prcnum=" << tkt.prcnum << BAEL_LOG_END;
            momsrvr::AQIListener::callToMomsrvrBusinessProcessMutex.unlock();
            throw;
        }

        momsrvr::AQIListener::callToMomsrvrBusinessProcessMutex.unlock();
    }
}

bool MQListener::setProcessor()
{
    BAEL_LOG_SET_CATEGORY("M_MOMSRVR.MQLISTENER.SETPROCESSOR");

    bslma::ManagedPtr<actionsvclient::IClient> actionsvClient(new actionsvclient::Client());
    bslma::ManagedPtr<momsrvr::IUtils> utils(new momsrvr::Utils(actionsvClient));
    aimcommon::Version clientVersion(momsrvr::momsrvr_business::FLOWBOT_SERVICE_MAJOR,
                                     momsrvr::momsrvr_business::FLOWBOT_SERVICE_MINOR);
    bslma::ManagedPtr<flowbotclient::IClient> flowbotClient(
        new flowbotclient::Client(clientVersion));

    momsrvr::momsrvr_business* mbus = new momsrvr::momsrvr_business(utils, flowbotClient);

    // Init connection to MOMDB
    string db("MOMDB");
    momtkt_DbInterface* db_c;

    try {
        db_c = momdb::Comdb2Connection::createConnection(db);
    } catch (momdb::MomDbException& m) {
        BAEL_LOG_ERROR << __func__ << ": Error connecting to momdb. error=" << m.getError().c_str()
                       << BAEL_LOG_END;
        return false;
    }

    momTktImpl* tktImp = new momTktImpl();
    tktImp->setDbInterface(db_c);

    mbus->setTktInterface(tktImp);
    mproc = mbus;

    return true;
}

// set different channel table for SAT.
void MQListener::setMQEnvSAT()
{
    BAEL_LOG_SET_CATEGORY("M_MOMSRVR.MQLISTENER.SETMQENVSAT");
    // Need to use static string for putenv
    static std::string var_MQCHLLIB = "MQCHLLIB=";
    static std::string var_MQCHLTAB = "MQCHLTAB=";

    // chl tab name from machine
    int machine_id = machine_();
    const char* podName = tschkpod_cpu2pod(machine_id);
    const char* chl_table = tschkpod_pod2channel(podName);

    // find last position of / in channel table path
    const char* namePos = strrchr(chl_table, '/');

    // split path string into basename and dirname
    var_MQCHLLIB += std::string(chl_table, namePos - chl_table);
    var_MQCHLTAB += std::string(namePos + 1);

    BAEL_LOG_INFO << var_MQCHLLIB << BAEL_LOG_END;
    BAEL_LOG_INFO << var_MQCHLTAB << BAEL_LOG_END;

    putenv((char*)var_MQCHLLIB.c_str());
    putenv((char*)var_MQCHLTAB.c_str());
}

void MQListener::getSTKSize()
{
    BAEL_LOG_SET_CATEGORY("M_MOMSRVR.MQLISTENER.GETSTKSIZE");
    const char* stk_size = getenv("AIXTHREAD_STK");
    BAEL_LOG_INFO << "AIXTHREAD_STK=" << stk_size << BAEL_LOG_END;
}

int MQListener::getSelectionString(ostringstream& pxListStr)
{
    pxListStr << "Root.MQMD.UserIdentifier IN (";
    int pxListLength = momsrvr::PxList::pricingNumbers.size();
    for (int i = 0; i < pxListLength; i++) {
        if (i > 0)
            pxListStr << ",";
        pxListStr << "'" << setiosflags(ios::left) << setw(12) << momsrvr::PxList::pricingNumbers[i]
                  << "'";
    }

    pxListStr << ")";

    return pxListLength;
}

bool MQListener::initializeBackoutQueueParameters()
{
    BAEL_LOG_SET_CATEGORY("M_MOMSRVR.MQLISTENER.INITIALIZEBACKOUTQUEUEPARAMETERS");

    // MQINQ Parameter Setup
    MQLONG selectors[] = { MQIA_BACKOUT_THRESHOLD, MQCA_BACKOUT_REQ_Q_NAME };
    MQLONG intAttributes[1];
    MQCHAR charAttributes[MQ_Q_NAME_LENGTH + 1];
    MQLONG completionCode = MQCC_OK;
    MQLONG reason = MQRC_NONE;
    memset(charAttributes, 0, sizeof(charAttributes));

    MQINQ(m_conn.hdl, m_queue.hdl, 2, selectors, sizeof(intAttributes), intAttributes,
          sizeof(charAttributes), charAttributes, &completionCode, &reason);

    if (MQCC_OK != completionCode) {
        BAEL_LOG_ERROR << "Unable to get backout queue parameters."
                       << " qName=" << m_qName << " completionCode=" << completionCode
                       << " reason=" << reason << BAEL_LOG_END;
        return false;
    }

    m_backoutThreshold = intAttributes[0];
    strncpy(m_backoutQueueName, charAttributes, sizeof(m_backoutQueueName));

    BAEL_LOG_INFO << "Successfully retrieved backout queue parameters."
                  << " qName=" << m_qName << " backoutQueueName=" << m_backoutQueueName
                  << " backoutThreshold=" << m_backoutThreshold << BAEL_LOG_END;

    return true;
}
