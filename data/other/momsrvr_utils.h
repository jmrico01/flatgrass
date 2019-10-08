#ifndef INCLUDED_MOMSRVR_UTILS
#define INCLUDED_MOMSRVR_UTILS

#ifndef INCLUDED_MOMSRVR_FORWARD
#include <momsrvr_forward.h>
#endif

// Local includes

#ifndef INCLUDED_MOMSRVR_IUTILS
#include <momsrvr_iutils.h>
#endif

// External includes
#ifndef INCLUDED_ACTIONSVCLIENT_ICLIENT
#include <actionsvclient_iclient.h>
#endif

#ifndef INCLUDED_BSLMA_MANAGEDPTR
#include <bslma_managedptr.h>
#endif

#ifndef INCLUDED_BDLT_DATETZ
#include <bdlt_datetz.h>
#endif

namespace BloombergLP {
namespace momsrvr {

class Utils : public IUtils {
public:
    Utils();

    Utils(bslma::ManagedPtr<actionsvclient::IClient> actionsvClient);

    virtual bool loadMomTicket(momtkt& ticket) const;

    virtual bool loadParmSubset() const;

    virtual int sendTktOut(momtkt& ticket) const;

    virtual int sendToTradeFeed(momtkt& ticket, int checkTasu, int autoRelease) const;

    virtual bool isFirmReleaseSet(momtkt& ticket) const;

    virtual int convertDateFromIntToDateTz(const int dateAsInt, bdlt::DateTz& dateAsDateTz) const;

private:
    bslma::ManagedPtr<actionsvclient::IClient> d_actionsvClient;
    enum FtrnLoadOption { NO_FTRN_LOAD = 0, LOAD_FTRN = 1, NO_VCON_CONF_CHK = 101 };

    static void auditTkt(int step, momtkt& t);
    static bool isSwapTicket();
    static bool isTPREligibleForRelease(momtkt& t);
    static bool isVCONConfirmed();
    static bool isAllocTkt(momtkt& tk);

    int sendToTCTM(momtkt& tk) const;
    int sendToAlloc(momtkt& tk) const;
    int sendToTradeFeed(momtkt& tk) const;
    int sendToCustody(momtkt& tk) const;
};

} // namespace momsrvr
} // namespace BloombergLP

#endif
