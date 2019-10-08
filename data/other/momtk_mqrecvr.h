#ifndef INCLUDED_MOMTK_MQRECVR
#define INCLUDED_MOMTK_MQRECVR

#include <sysutil_ident.h>
SYSUTIL_IDENT_RCSID(momtk_mqrecvr_h, "$Id$ $CSID$")
SYSUTIL_PRAGMA_ONCE

#include <bsl_string.h>
#include <bsls_atomic.h>
#include <iomanip>
#include <iostream>
#include <momtk_processInterface.h>
#include <pthread.h>

extern "C" {
#include <btsmi.h>
}

using namespace std;

#define MAX_MQ_MESSAGE_SIZE 4096

/**
 * A Simple MQ listener on a specified QM queue.
 *
 * @author Rajesh Chandrasekaran
 * @version 1.0
 */

class MQListener {
public:
    typedef bsl::string MQListenerException;

    MQListener();
    ~MQListener();

    // main method. loops and tries to get MQ messages
    void run();

private:
    static const int TIMEOUT_INTERVAL = 10000; // 1000th of a second
    static const int MAX_MSG_RETRY = 10;

    bool setProcessor();

    void getSTKSize();
    void setMQEnvSAT();

    // connect to queue manager
    bool connect();
    // disconnect from MQ
    bool disconnect();
    // reconnect to MQ
    bool reconnect();

    // open MQ
    bool openQueues();
    // close MQ
    bool closeQueues();

    /**
     * helper to open queue for getting messages
     * @param qName queue name
     * @param qmName queue manager name
     * @return if success
     */
    bool openListenQueue(char* qName, char* qmName);

    /**
     * helper to open queue for putting messages
     * @param qName queue name
     * @param qmName queue manager name
     * @return if success
     */
    bool openSendQueue(char* qName, char* qmName);

    bool checkConnection();

    int getSelectionString(ostringstream& pxListStr);
    bool initializeBackoutQueueParameters();

    void handle(const char* msgBuf, btsmi_LONG msgBufSz);

    momtkt_ProcessInterface* mproc;

    btsmi_CONNECTION m_conn;
    btsmi_QUEUE m_queue;
    btsmi_QUEUE m_backoutQueue;

    char m_qName[MQ_Q_NAME_LENGTH + 1];
    char m_backoutQueueName[MQ_Q_NAME_LENGTH + 1];
    char m_qmName[MQ_Q_MGR_NAME_LENGTH + 1];
    // if about to terminate
    bsls::AtomicBool m_done;
    // if connected to the queue manager
    volatile bool m_connected;
    // if all queues are open
    volatile bool m_openedQueues;
    // if queues need to be reopened
    volatile bool m_shouldReopen;

    pthread_t m_listenerThread;

    int m_retryCount;
    int m_emptyPxCount;

    int m_backoutThreshold;

    // Stores value of BREG 316636
    bool m_enableBackoutQueue;
};

#endif // INCLUDED_MOMTK_MQRECVR
