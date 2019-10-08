#include <sysutil_ident.h>
SYSUTIL_IDENT_RCSID(momsrvr_m_cpp, "$Id$ $CSID$")
#include <bsl_sstream.h>
#include <iostream>

// Queue listeners
#include <bael_log.h>
#include <bascfg_loggingconfig.h>
#include <bassvc_loggermanager.h>
#include <bdet_datetimeinterval.h>
#include <bsl_exception.h>
#include <bsl_string.h>
#include <bslma_managedptr.h>
#include <ctrace.h>
#include <dbutil.h>
#include <gtout_publisherguard.h>
#include <gutz_metrics.h>
#include <momsrvr_aqilistener.h>
#include <momsrvr_pxlist.h>
#include <momtk_mqrecvr.h>
#include <stdio.h>
#include <stdlib.h>
#include <tsofflineinit_initialize.h>

// bscclientdefault
#include <basapi_tcpclient.h>
#include <basapi_tcpsyncbasclient.h>
#include <bscapi_syncbasclientutil.h>

// Bregs
#include <bbit/201902/bbit_read_rabbitmq_tradeworkfloweventsconsumer.h>

extern "C" {

void pekludgl_();
void f77override(int, char**);
void initque_();
void pt_msgtrap_(int* pmsg);
int machine_();

#include <gtrade_wrserr.h>
#include <logger.h>
#include <pekludge.h>
#include <ptdispatch_ptdispatch.h>
}

// Set up BB threads, M-trap, Q-trap, timer etc.
static bool inputProc(bassvc::LoggerManager& loggerManager,
                      BloombergLP::momsrvr::AQIListener* aqiListener);
static bool momsrvrtrap(const char* msg, bassvc::LoggerManager& loggerManager,
                        BloombergLP::momsrvr::AQIListener* aqiListener);

//=============================================================================
//                                 MAIN PROGRAM
//-----------------------------------------------------------------------------
using namespace std;

int main(int argc, char* argv[])
{
    f77override(argc, argv);

    if (argc < 3) {
        bsl::cerr << "momsrvr-usage: momsrvr[12] <taskname> <taskid>" << bsl::endl;
        return 1;
    }

    bsl::string taskName(argv[1]);
    int taskID = atoi(argv[2]);

    pekludge_(&taskID);

    // M-trap init..s
    setpgrp();
    initque_(); // initialize traps
    enablex(3, pt_msgtrap_); // enable send to me
    ldgbl(taskID);

    bascfg::LoggingConfig config;
    config.publishInLocalTime() = true;
    config.filename() = "/ts/log/mo/momsrvr.log.%T";
    config.maxSize() = 1024 * 256;
    config.rotationPeriod() = 24 * 60 * 60; // every 24 hours
    config.stdoutLoggingThreshold() = 1; // ERROR
    config.verbosityLevel() = 3; // INFO
    bassvc::LoggerManager loggerManager;
    loggerManager.initialize(config);
    loggerManager.printLogFile();

    gtout::PublisherConfig gutsConfig;
    gtout::NamespaceTags namespaceTags;
    namespaceTags.metricNamespace() = "momsrvr";
    gtout::Tag tag;
    tag.key() = "instanceName";
    tag.value() = "momsrvr";
    namespaceTags.tags().push_back(tag);
    gutsConfig.namespaceTags().push_back(namespaceTags);
    gtout::PublisherGuard metricsGuard(gutsConfig);

    // bscclientdefault install TCP client as default
    basapi::TcpClient tcpClient;
    basapi::TcpSyncBasClient tcpSyncBasClient(&tcpClient);
    bscapi::SyncBasClientScopedGuard guard(&tcpSyncBasClient);

    BAEL_LOG_SET_CATEGORY("M_MOMSRVR.MAIN");

    BAEL_LOG_INFO << "Starting momsrvr taskName=" << taskName << " taskID=" << taskID
                  << BAEL_LOG_END;

    // MBUS Library init
    tsofflineinit_initialize();

    // Get list of pricing numbers
    BloombergLP::momsrvr::PxList::initialize();

    // Create and initialize IBM MQ listener
    bslma::ManagedPtr<MQListener> mqListener;
    try {
        mqListener.load(new MQListener());
    } catch (MQListener::MQListenerException e) {
        BAEL_LOG_ERROR << "Error creating IBM MQ listener. error=" << e << BAEL_LOG_END;
        return 1;
    }

    // Create and initialize AQI listener (if BREG is enabled)
    bslma::ManagedPtr<BloombergLP::momsrvr::AQIListener> aqiListener;
    if (bbit_read_rabbitmq_tradeworkfloweventsconsumer__value()) {
        try {
            aqiListener.load(new BloombergLP::momsrvr::AQIListener(taskName));
        } catch (BloombergLP::momsrvr::AQIListener::AQIListenerException e) {
            BAEL_LOG_ERROR << "Error creating AQI listener. error=" << e << BAEL_LOG_END;
            return 1;
        }
    }

    // Listen and process msg traps
    if (!inputProc(loggerManager, aqiListener.get())) {
        BAEL_LOG_ERROR << "inputProc failed" << BAEL_LOG_END;
        return 1;
    }

    BAEL_LOG_INFO << "Exiting momsrvr main" << BAEL_LOG_END;
    return 0;
}

static bool inputProc(bassvc::LoggerManager& loggerManager,
                      BloombergLP::momsrvr::AQIListener* aqiListener)
{
    BAEL_LOG_SET_CATEGORY("M_MOMSRVR.INPUTPROC");

    // Initialize thread that transfers msg traps to queue
    BAEL_LOG_INFO << "Starting waitft.." << BAEL_LOG_END;
    ptdispatch_init(64000);
    waitft_return_not_exit(); // On EXIT mtrap, waitft will return instead of calling exit(0)
    pthread_t tid;
    int status = pthread_create(&tid, &PtAttr, pt_waitft, NULL);
    if (status != 0) {
        BAEL_LOG_ERROR << "Failed to start waitft thread, status=" << status << BAEL_LOG_END;
        return false;
    }

    bool done = false;
    while (!done) {
        PTUI* ptui = pt_dequeue(PTINQ);

        switch (ptui->id) {
        case PT_MSGTRAP: {
            bool shouldContinue = momsrvrtrap((const char*)ptui->pdata, loggerManager, aqiListener);
            if (!shouldContinue) {
                // momsrvrtrap got an exit signal
                done = true;
            }
            pt_return_buf(ptui);
            break;
        }
        default:
            BAEL_LOG_ERROR << "Unknown PTUI " << ptui->id << BAEL_LOG_END;
            pt_return_buf(ptui);
        }
    }

    // Attempt to join waitft thread, though it will likely be dead at this point (status ESRCH)
    int joinStatus = pthread_join(tid, NULL);
    if (joinStatus != 0 && joinStatus != ESRCH) {
        BAEL_LOG_ERROR << "Failed to join waitft thread, status=" << joinStatus << BAEL_LOG_END;
        return false;
    }

    return true;
}

static bool momsrvrtrap(const char* msg, bassvc::LoggerManager& loggerManager,
                        BloombergLP::momsrvr::AQIListener* aqiListener)
{
    BAEL_LOG_SET_CATEGORY("M_MOMSRVR.MOMSRVRTRAP");

    char msgbuffer[80];
    memcpy(msgbuffer, msg + 8, 80);
    for (int i = 0; i < 80; i++) {
        msgbuffer[i] = toupper(msgbuffer[i]);
    }

    BAEL_LOG_DEBUG << msgbuffer << BAEL_LOG_END;
    if (!memcmp(msgbuffer, "EXIT", 4)) {
        BAEL_LOG_INFO << "Got msg trap mtrap=EXIT" << BAEL_LOG_END;
        gblshrrm_();
        return false;
    } else if (!memcmp(msgbuffer, "GETPX", 5)) {
        BAEL_LOG_INFO << "Got msg trap mtrap=GETPX" << BAEL_LOG_END;
        BloombergLP::momsrvr::PxList::initialize();
    } else if (!memcmp(msgbuffer, "PRINT", 5)) {
        BAEL_LOG_INFO << "Got msg trap mtrap=PRINT" << BAEL_LOG_END;
        BloombergLP::momsrvr::PxList::print();
    } else if (!memcmp(msgbuffer, "PXON", 4)) {
        BAEL_LOG_INFO << "Got msg trap mtrap=PXON" << BAEL_LOG_END;
        const char* pxString = strtok(msgbuffer + 4, " ");
        int px = atoi(pxString);
        BloombergLP::momsrvr::PxList::addNewPxToList(px);
        if (bbit_read_rabbitmq_tradeworkfloweventsconsumer__value()) {
            if (!aqiListener->initializeQueue(px)) {
                BAEL_LOG_ERROR << "Unable to initialize queue for prcnum=" << px << BAEL_LOG_END;
            }
        }
    } else if (!memcmp(msgbuffer, "PXOF", 4)) {
        BAEL_LOG_INFO << "Got msg trap mtrap=PXOF" << BAEL_LOG_END;
        const char* pxString = strtok(msgbuffer + 4, " ");
        int px = atoi(pxString);
        BloombergLP::momsrvr::PxList::removePxFromList(px);
        if (bbit_read_rabbitmq_tradeworkfloweventsconsumer__value()) {
            if (!aqiListener->terminateQueue(px)) {
                BAEL_LOG_ERROR << "Unable to terminate queue for prcnum=" << px << BAEL_LOG_END;
            }
        }
    } else if (!memcmp(msgbuffer, "VERB", 4)) {
        BAEL_LOG_INFO << "Got msg trap mtrap=VERB" << BAEL_LOG_END;
        bsl::stringstream parser;
        parser << msgbuffer;
        bsl::string command;
        int verbosity;
        parser >> command >> verbosity;
        loggerManager.setVerbosityLevel(verbosity);
        BAEL_LOG_DEBUG << "MOMSRVR - received verbosity change m-trap. new "
                          "passthrough verbosity: "
                       << verbosity << "." << BAEL_LOG_END;
    } else {
        BAEL_LOG_ERROR << "Got unsupported msg trap mtrap=" << msgbuffer << BAEL_LOG_END;
    }

    return true;
}
