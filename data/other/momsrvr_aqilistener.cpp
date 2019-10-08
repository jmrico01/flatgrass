#include <momsrvr_aqilistener.h>

#include <actionsvclient_client.h>
#include <amcat_schemautil.h>
#include <bael_log.h>
#include <bdlf_bind.h>
#include <bsl_map.h>
#include <bsl_string.h>
#include <bsl_vector.h>
#include <bslma_managedptr.h>
#include <bslmt_lockguard.h>
#include <flowbotclient_client.h>
#include <gutz_metrics.h>
#include <is_aim_prcnum.h>
#include <momdb_comdb2conn.h>
#include <momsrvr_processor.h>
#include <momsrvr_utils.h>
#include <momtk_DbInterface.h>
#include <momtk_tktimpl.h>
#include <tmib_atlasmsgcontext.h>
#include <tmib_consumersession.h>
#include <tssrbigutil_pnumutil.h>

#include <bbit/201902/bbit_process_rabbitmq_tradeworkfloweventsconsumer.h> // BREG 345667

namespace {
const char LOG_CATEGORY[] = "MOMSRVR.AQILISTENER";
const string GUTS_NAMESPACE_ROOT = "momsrvr";
const string GUTS_NAMESPACE_MOMSRVR = "aqilistener";
const string GUTS_METRIC_MOMSRVR_FUNCTION = GUTS_NAMESPACE_MOMSRVR + ".function";
const string GUTS_METRIC_MOMSRVR_MUTEX = GUTS_NAMESPACE_MOMSRVR + ".mutex";
const string GUTS_TAG_FUNCTION_NAME = "functionName";
const string GUTS_TAG_PRICING_NUMBER = "pricingNumber";
} // namespace

namespace BloombergLP {
namespace momsrvr {

bslmt::Mutex AQIListener::callToMomsrvrBusinessProcessMutex;

const bsl::string AQIListener::ENDPOINT = "atlas.mq.endpoint.Atlas2.TradeWorkflowEventsConsumer";
const bsl::string AQIListener::APP_ID = "883750";

AQIListener::AQIListener(const bsl::string& taskName)
    : d_taskName(taskName)
{
    BAEL_LOG_SET_CATEGORY(LOG_CATEGORY);

    BAEL_LOG_INFO << __func__ << ": Attempting to connect to AQI listener..." << BAEL_LOG_END;

    // Sets interface for db and message processor
    if (!setProcessor()) {
        BAEL_LOG_ERROR
            << __func__
            << ": Failed to set AQI listener processor error_type=failed_setting_processor_aqi"
            << BAEL_LOG_END;
        throw AQIListenerException("setProcessor error");
    }

    // Initialize queue sessions for all pricing numbers
    if (!initializeAllQueues()) {
        BAEL_LOG_ERROR << __func__
                       << ": Failed to initialize queues for AQI listener "
                          "error_type=failed_initialize_queue_aqi"
                       << BAEL_LOG_END;
        throw AQIListenerException("initializeAllQueues error");
    }

    BAEL_LOG_INFO << __func__ << ": Initialized AQI listener" << BAEL_LOG_END;
}

AQIListener::~AQIListener()
{
    BAEL_LOG_SET_CATEGORY(LOG_CATEGORY);

    for (ConsumerSessions::iterator it = d_sessions.begin(); it != d_sessions.end(); it++) {
        BAEL_LOG_INFO << __func__ << ": Stopping consumer session for prcnum=" << it->first
                      << BAEL_LOG_END;

        tmiv::RCode::Value rcode = it->second->stop();
        if (rcode != tmiv::RCode::SUCCESS) {
            BAEL_LOG_ERROR << __func__ << ": Failed to stop consumer session. rcode=" << rcode
                           << " error_type=failed_stop_consumer_session_aqi" << BAEL_LOG_END;
        }
    }

    d_sessions.clear();

    BAEL_LOG_INFO << __func__ << ": Stopped all AQI listener sessions" << BAEL_LOG_END;
}

bool AQIListener::initializeAllQueues()
{
    BAEL_LOG_SET_CATEGORY(LOG_CATEGORY);

    bsl::vector<int> pricingNumbers = tssrbigutil::PnumUtil::getPxonPnumList();
    BAEL_LOG_DEBUG << __func__ << ": Pricing numbers list size: " << pricingNumbers.size()
                   << BAEL_LOG_END;

    for (bsl::vector<int>::iterator it = pricingNumbers.begin(); it != pricingNumbers.end(); ++it) {
        if (!initializeQueue(*it)) {
            return false;
        }
    }
    return true;
}

bool AQIListener::initializeQueue(int prcnum)
{
    BAEL_LOG_SET_CATEGORY(LOG_CATEGORY);

    if (d_sessions.find(prcnum) != d_sessions.end()) {
        BAEL_LOG_ERROR << __func__ << ": There is an existing session for this prcnum=" << prcnum
                       << " error_type=session_existed_aqi" << BAEL_LOG_END;
        return true;
    }

    if (prcnum <= 0) {
        BAEL_LOG_ERROR << __func__ << ": Pricing number must be greater than 0.  prcnum=" << prcnum
                       << " is invalid. error_type=invalid_pricing_number_aqi" << BAEL_LOG_END;
        return false;
    }

    if (!is_aim_prcnum(prcnum)) {
        BAEL_LOG_INFO << __func__ << ": Pricing number prcnum=" << prcnum
                      << " is not AIM pricing number. Skip opening the queue" << BAEL_LOG_END;
        return true;
    }

    tmiv::SessionOptions options;
    options.asyncRecvThreadPoolSize() = 1; // Single thread to process incoming messages
    options.consumerConfirm() = 1; // Explicitly ack processed messages
    options.asyncRecvThreadPoolCapacity() = 5; // Small backlog

    BAEL_LOG_INFO << __func__ << ": Creating session for prcnum=" << prcnum << BAEL_LOG_END;
    options.clientIsolationInfo().pricingNumber().makeValue(prcnum);

    // Set up context
    bsl::shared_ptr<tmib::AtlasMsgContext> tmiContext(
        new tmib::AtlasMsgContext(APP_ID, d_taskName));

    ConsumerSessionPtr session = tmiContext->createConsumerSession(ENDPOINT, options);

    if (!session) {
        BAEL_LOG_ERROR << __func__ << ": Failed to create consumer session. endpoint=" << ENDPOINT
                       << " error_type=failed_create_consumer_session_aqi" << BAEL_LOG_END;
        return false;
    }

    BAEL_LOG_INFO << __func__ << ": Start consumer session for prcnum=" << prcnum << BAEL_LOG_END;
    tmiv::RCode::Value rcode = session->start(
        bdlf::BindUtil::bind(&AQIListener::messageHandler, this, session.get(),
                             bdlf::PlaceHolders::_1),
        bdlf::BindUtil::bind(&AQIListener::errorHandler, this, bdlf::PlaceHolders::_1,
                             bdlf::PlaceHolders::_2, bdlf::PlaceHolders::_3));

    if (tmiv::RCode::SUCCESS != rcode) {
        BAEL_LOG_ERROR << __func__ << ": Failed to start consumer session. rcode=" << rcode
                       << " error_type=failed_start_consumer_session_aqi" << BAEL_LOG_END;
        return false;
    }

    d_sessions[prcnum] = session;

    return true;
}

bool AQIListener::terminateQueue(int prcnum)
{
    BAEL_LOG_SET_CATEGORY(LOG_CATEGORY);

    BAEL_LOG_INFO << __func__ << ": For prcnum=" << prcnum << " stop consumer session"
                  << BAEL_LOG_END;

    tmiv::RCode::Value rcode = d_sessions[prcnum] ? d_sessions[prcnum]->stop()
                                                  : tmiv::RCode::SUCCESS;
    if (tmiv::RCode::SUCCESS != rcode) {
        BAEL_LOG_ERROR << __func__ << ": Failed to stop consumer session. [rcode=" << rcode << "]"
                       << " error_type=failed_stop_consumer_session_aqi" << BAEL_LOG_END;
        return false;
    }

    d_sessions.erase(prcnum);

    return true;
}

bool AQIListener::setProcessor()
{
    BAEL_LOG_SET_CATEGORY(LOG_CATEGORY);

    bslma::ManagedPtr<actionsvclient::IClient> actionsvClient(new actionsvclient::Client());
    bslma::ManagedPtr<momsrvr::IUtils> utils(new momsrvr::Utils(actionsvClient));
    aimcommon::Version clientVersion(momsrvr::momsrvr_business::FLOWBOT_SERVICE_MAJOR,
                                     momsrvr::momsrvr_business::FLOWBOT_SERVICE_MINOR);
    bslma::ManagedPtr<flowbotclient::IClient> flowbotClient(
        new flowbotclient::Client(clientVersion));

    // Init connection to MOMDB
    string db("MOMDB");
    momtkt_DbInterface* db_c;

    try {
        db_c = momdb::Comdb2Connection::createConnection(db);
    } catch (momdb::MomDbException& m) {
        BAEL_LOG_ERROR << __func__ << ": Error connecting to momdb. error=" << m.getError().c_str()
                       << " error_type=error_connect_momdb_aqi" << BAEL_LOG_END;
        return false;
    }

    momTktImpl* tktImp = new momTktImpl();
    tktImp->setDbInterface(db_c);

    momsrvr::momsrvr_business* mbus = new momsrvr::momsrvr_business(utils, flowbotClient);

    mbus->setTktInterface(tktImp);
    mproc = mbus;

    return true;
}

void AQIListener::messageHandler(tmib::ConsumerSession* session,
                                 const bsl::shared_ptr<tmiv::Message>& message)
{
    BAEL_LOG_SET_CATEGORY(LOG_CATEGORY);
    const char* GUTS_VALUE_FUNCTION_NAME = "messageHandler";

    bool ack = true;
    const tmiv::MessageSource messageSource = message->messageSource();
    BAEL_LOG_DEBUG << __func__ << " messageSource: " << messageSource << BAEL_LOG_END;

    const int prcnum = messageSource.pricingNumber();
    bsl::shared_ptr<tmiv::MessageBuilder> messageBuilder = session->getMessageBuilder();
    if (!messageBuilder) {
        BAEL_LOG_ERROR << __func__ << ": No message builder for prcnum=" << prcnum
                       << " error_type=no_message_builder_aqi" << BAEL_LOG_END;
        return;
    }

    GUTZ_METRICS_TAGGED_COUNTER(GUTS_NAMESPACE_ROOT.c_str(), GUTS_METRIC_MOMSRVR_FUNCTION.c_str(),
                                1, GUTS_TAG_FUNCTION_NAME.c_str(), GUTS_VALUE_FUNCTION_NAME,
                                GUTS_TAG_PRICING_NUMBER.c_str(), bsl::to_string(prcnum));

    const bsl::string messageIntent = message->messageIntent();
    if (messageIntent == AQIDecoder::NEW_TRADE || messageIntent == AQIDecoder::AMEND_TRADE) {
        BAEL_LOG_DEBUG << __func__ << ": Received trade for prcnum=" << prcnum
                       << " messageIntent=" << messageIntent << BAEL_LOG_END;

        amcat::Response response;
        if (!messageBuilder->decodeMessagePayload(&response, message, tmiv::EncodingType::XML)) {
            BAEL_LOG_ERROR << __func__ << ": Error decoding MessagePayload for prcnum=" << prcnum
                           << " error_type=error_decode_message_aqi" << BAEL_LOG_END;
            message->reject(!message->redelivered());
            return;
        }

        amcat::LegacyTradesResponse& legacyTrades = response.legacyTrades();

        if (legacyTrades.legacyTrade().empty()
            || (!legacyTrades.enrichedLegacyTrade().empty()
                && (legacyTrades.legacyTrade().size()
                    != legacyTrades.enrichedLegacyTrade().size()))) {
            BAEL_LOG_ERROR << __func__
                           << ": Error getting legacyTrades from AQI for prcnum=" << prcnum
                           << " error_type=error_getting_legacytrades_aqi" << BAEL_LOG_END;
            message->reject(!message->redelivered());
            return;
        }

        for (bsl::size_t index = 0; index < legacyTrades.legacyTrade().size(); ++index) {
            momtkt tkt;
            AQIDecoder::AQITcTicketDataRecord tcTicketData(
                legacyTrades.legacyTrade()[index], legacyTrades.enrichedLegacyTrade()[index], 0);
            {
                GUTZ_METRICS_TAGGED_DISTRIBUTION_TIMER_MS(
                    GUTS_NAMESPACE_ROOT.c_str(), GUTS_METRIC_MOMSRVR_FUNCTION.c_str(),
                    GUTS_TAG_FUNCTION_NAME.c_str(), "constructMomtkt");

                if (AQIDecoder::RCode rc = AQIDecoder::constructMomtkt(tcTicketData, messageIntent,
                                                                       prcnum, tkt)) {
                    if (rc == AQIDecoder::TICKET_FAIL) {
                        BAEL_LOG_ERROR << __func__
                                       << ": Error loading AQI to momtkt for prcnum=" << prcnum
                                       << " error_type=error_construct_momtkt" << BAEL_LOG_END;
                        ack = false;
                    }
                    continue;
                }
            }

            BAEL_LOG_DEBUG << __func__ << ": Parsed momtkt for action=" << messageIntent
                           << " momtkt=[" << tkt.toMessageBuf(tkt) << "]" << BAEL_LOG_END;
            if (bbit_process_rabbitmq_tradeworkfloweventsconsumer__value()
                && (tkt.tktaction == ADD_TKT || tkt.tktaction == XPT_ALLOC_UPD)) {
                {
                    GUTZ_METRICS_TAGGED_DISTRIBUTION_TIMER_MS(
                        GUTS_NAMESPACE_ROOT.c_str(), GUTS_METRIC_MOMSRVR_MUTEX.c_str(),
                        GUTS_TAG_FUNCTION_NAME.c_str(), "callToMomsrvrBusinessProcessMutexLock",
                        GUTS_TAG_PRICING_NUMBER.c_str(), bsl::to_string(prcnum));
                    callToMomsrvrBusinessProcessMutex.lock();
                }
                try {
                    if (!mproc->process(tkt)) {
                        BAEL_LOG_ERROR << __func__ << "Failed to process ticket in momsrvr process,"
                                       << " error_type=failed_process_ticket_aqi"
                                       << " tktnum=" << tkt.tktnum << " prcnum=" << tkt.prcnum
                                       << BAEL_LOG_END;
                    }
                } catch (...) {
                    BAEL_LOG_ERROR << __func__ << "Exception caught during momsrvr process"
                                   << " error_type=exception_aqi" << BAEL_LOG_END;
                }
                callToMomsrvrBusinessProcessMutex.unlock();
            }
        }

    } else if (messageIntent == AQIDecoder::CANCEL_TRADE
               || messageIntent == AQIDecoder::DELETE_TRADE
               || messageIntent == AQIDecoder::DELETE_ALLOC) {
        BAEL_LOG_DEBUG << __func__ << ": Received trade for prcnum=" << prcnum
                       << " messageIntent=" << messageIntent << BAEL_LOG_END;
    } else {
        BAEL_LOG_WARN << __func__ << ": Unrecognized message type, messageIntent=" << messageIntent
                      << BAEL_LOG_END;
    }

    if (ack) {
        message->acknowledge();
    } else {
        // not to requeue if it is already a redelivered message
        message->reject(!message->redelivered());
    }
}

void AQIListener::errorHandler(tmib::ConsumerSession* session, tmiv::RCode::Value errorCode,
                               const bsl::string& errorMessage)
{
    BAEL_LOG_SET_CATEGORY(LOG_CATEGORY);

    BAEL_LOG_ERROR << __func__ << ": Error in consumer session: "
                   << "errorCode=" << errorCode << ", error=" << errorMessage << BAEL_LOG_END;

    // reject -> add to deadletter or retry
}

} // namespace momsrvr
} // namespace BloombergLP
