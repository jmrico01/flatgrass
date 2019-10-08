#ifndef INCLUDED_MOMSRVR_PROCESSOR
#define INCLUDED_MOMSRVR_PROCESSOR

#ifndef INCLUDED_MOMSRVR_FORWARD
#include <momsrvr_forward.h>
#endif

// Local includes

#ifndef INCLUDED_MOMSRVR_IUTILS
#include <momsrvr_iutils.h>
#endif

// External includes

#ifndef INCLUDED_AIMCOMMON_FORWARD_H
#include <aimcommon_forward.h>
#endif

#ifndef INCLUDED_BSLMA_MANAGEDPTR
#include <bslma_managedptr.h>
#endif

#ifndef INCLUDED_FLOWBOTCLIENT_ICLIENT
#include <flowbotclient_iclient.h>
#endif

#ifndef INCLUDED_MOMTK_PROCESSINTERFACE_H
#include <momtk_processInterface.h>
#endif

#ifndef INCLUDED_FLOWBOTCLIENT_MESSAGES
#include <flowbotclient_messages.h>
#endif

#include <bsl_unordered_map.h>

using namespace std;

namespace BloombergLP {
namespace momsrvr {

struct IsFlowStart {
    static const bool True;
    static const bool False;
};

struct IsFlowEnd {
    static const bool True;
    static const bool False;
};

class momsrvr_business : public momtkt_ProcessInterface {
public:
    static const unsigned FLOWBOT_SERVICE_MAJOR = 1;
    static const unsigned FLOWBOT_SERVICE_MINOR = 0;

    explicit momsrvr_business(bslma::ManagedPtr<IUtils> utils);
    explicit momsrvr_business(bslma::ManagedPtr<IUtils> utils,
                              bslma::ManagedPtr<flowbotclient::IClient> flowbotClient);
    virtual ~momsrvr_business();
    virtual bool process(momtkt& ticket);
    virtual void setTktInterface(momtkt_TktInterface* dbi);

    enum TICKET_VALIDATION {
        MOMSRVR_VALID_TICKET = 0,
        MOMSRVR_INVALID_TICKET = 1,
        MOMSRVR_BYPASS_TICKET = 2
    };

private:
    momtkt_TktInterface* d_ticketInterface;
    bslma::ManagedPtr<flowbotclient::IClient> d_flowbotClient;
    bslma::ManagedPtr<IUtils> d_utils; // Invariant: This pointer is never null

    bsl::unordered_map<int, string> actionNumberToString;

    bool validTCTMTkt(momtkt& ticket);
    short getDerivativeType();

    IUtils& getUtils();

    /**
     * Function to retrieve the current state and the next state of the ticket
     * according to the automated workflow setup
     *
     * @param ticket The output parameter whose current state and next state
     * will be updated
     * @param isStartState Indicator of whether the ticket is at the first state
     * in the auto workflow
     * @return Code that indicates the status of querying the auto workflow
     * setup
     * */
    FlowReturnCode getFirmFlow(aimcommon::OutParam<momtkt> ticket, bool isStartState);

    /**
     * Function to initiate an automated workflow for the provided ticket
     *
     * @param ticket Ticket for which a new workflow needs to be created
     * @return Code that indicates the status of adding the ticket flow
     * */
    FlowReturnCode addTicketFlow(const momtkt& ticket);

    /**
     * Function to process a UPD_TKT signal for the given ticket.
     *
     * @param ticket Ticket for which we're processing the UPD_TKT signal.
     * @return Code that indicates the status of process the ticket update.
     * */
    FlowReturnCode processUpdateTicket(momtkt& ticket);

    /**
     * Function to move the ticket to the next step in the auto workflow
     *
     * @param ticket Ticket which we want to move forward to the next step
     * @param currentStepStatus Whether the current step has been completed or
     * not
     * @return Code that indicates the status of updating the ticket flow
     * */
    FlowReturnCode moveToNextFlowStep(aimcommon::OutParam<momtkt> ticket, int currentStepStatus);

    /**
     * Function to retrieve flags for the current step for the ticket flow
     *
     * @param autoRelease Flag that indicates whether the current step should be
     * auto released
     * @param waitForStatus Flag that indicates whether the next step should
     * wait for the current step to complete
     * @return Code that indicates the status of querying the auto workflow
     * setup
     * */
    FlowReturnCode getFlowFlags(const momtkt& ticket, aimcommon::OutParam<bool> autoRelease,
                                aimcommon::OutParam<bool> waitForStatus);

    /**
     * Function to determine whether the given ticket's current step
     * has been released and/or completed.
     *
     * @param ticket Ticket (and current step) to retrieve status from
     * @param stepReleased Whether the ticket has already been released by
     * momsrvr
     * @param readyForNextStep Whether momsrvr is done with the current step and
     * should move to the next
     * */
    bool getTicketStepStatus(momtkt& ticket, aimcommon::OutParam<bool> stepReleased,
                             aimcommon::OutParam<bool> readyForNextStep);

    /**
     * Function that converts the integer return codes of ticketInterface calls
     * to a set of return codes defined for momsrvr
     *
     * @param rcode Return code from calls to ticketInterface
     * @return Code that indicates the status of interacting with the auto
     * workflow setup
     * */
    FlowReturnCode convertToFlowReturnCode(int rcode);

    /**
     * Function that converts the flowbotclient::ReturnCode from calling
     * flowbotclient methods to a set of return codes defined for momsrvr
     *
     * @param rcode Return code from calls to flowbotclient
     * @return Code that indicates the status of interacting with the auto
     * workflow setup
     * */
    FlowReturnCode convertToFlowReturnCode(flowbotclient::ReturnCode rcode);

    /**
     * Function that converts the flowbot service type to the corresponding
     * momdb state macro
     * @param service Service type returned from flowbotclient
     * @return The corresponding momdb state
     * */
    int convertFlowbotActionToMomState(flowbotclient::Action::Value service);

    /**
     * Function to return the current step in the auto workflow
     *
     * @param ticket Ticket which we want to get the current step (a.k.a. curr_state)
     * @param isStartState Whether the ticket is at a start state
     * @return Code that indicates the status of getting the current step
     * */
    FlowReturnCode getCurrentFlowStep(momtkt& ticket, bool isStartState);

    /**
     * Function to return the current step status according to ticket.curr_state
     *
     * @param ticket Ticket which has a valid curr_state
     * @param stepStatus The current step status(e.g. MOM_STATE_IN_PROGRESS)
     * @return Boolean indicating if the function is processed or failed
     */
    bool getCurrentTktStepStatus(const momtkt& ticket, aimcommon::OutParam<int> stepStatus);

protected:
    virtual int processNewTkt(momtkt& ticket);
    virtual int processCxlCorrTkt(momtkt& ticket);
    virtual int processActiveTkt(momtkt& ticket);
    virtual int processAllocationUpdate(momtkt& ticket);
    virtual int processReporting(momtkt& ticket);
    virtual int checkCurrentStepStatus(momtkt& ticket);
    virtual int prepareMomProcess(momtkt& ticket);
    virtual bool initMomTktFlow(momtkt& ticket);
    virtual int updStepStatus(momtkt& ticket, int status);
    virtual int getFirstState(int prcnum, int dept, int* stepid);
    virtual bool tktManualReleased(momtkt& ticket);
    virtual bool getStepStatus(momtkt& ticket);
    virtual int processTktFlow(momtkt& ticket);
    virtual int updateMasterFlow(momtkt& ticket);
    virtual bool checkMomTkt(int pxnum, int tktnum, int trandate);
    virtual bool notMomFlowTkt(const momtkt& ticket);
};

} // namespace momsrvr
} // namespace BloombergLP

#endif
