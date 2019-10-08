#ifndef INCLUDED_MOMSRVR_AQILISTENER
#define INCLUDED_MOMSRVR_AQILISTENER

#include <amcat_response.h>
#include <amcsys_ticketdeletion.h>
#include <bael_log.h>
#include <bcec_queue.h>
#include <bdeut_variant.h>
#include <bsl_map.h>
#include <bsl_string.h>
#include <bsl_vector.h>
#include <bslmt_mutex.h>
#include <momsrvr_aqidecoder.h>
#include <momtk_processInterface.h>
#include <tmib_consumersession.h>

namespace BloombergLP {
namespace momsrvr {

class AQIListener {
    typedef bsl::shared_ptr<tmib::ConsumerSession> ConsumerSessionPtr;
    typedef bsl::map<int, ConsumerSessionPtr> ConsumerSessions; // <pricing number, session>

public:
    typedef bsl::string AQIListenerException;

    AQIListener(const bsl::string& taskName);
    ~AQIListener();

    // initialize queue for prcnum
    bool initializeQueue(int prcnum);

    // terminate queue for prcnum
    bool terminateQueue(int prcnum);

    // Mutex to control call to momsrvr_business::process
    static bslmt::Mutex callToMomsrvrBusinessProcessMutex;

private:
    static const bsl::string ENDPOINT;
    static const bsl::string APP_ID;

    // initalize queues on start up
    bool initializeAllQueues();

    // Set up momtkt_ProcessInterface
    bool setProcessor();

    void messageHandler(tmib::ConsumerSession* session,
                        const bsl::shared_ptr<tmiv::Message>& message);

    void errorHandler(tmib::ConsumerSession* session, tmiv::RCode::Value errorCode,
                      const bsl::string& errorMessage);

    momtkt_ProcessInterface* mproc;

    ConsumerSessions d_sessions;

    const bsl::string d_taskName;
};

} // namespace momsrvr
} // namespace BloombergLP

#endif // INCLUDED_MOMSRVR_AQILISTENER
