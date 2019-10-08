#ifndef INCLUDED_MOMSRVR_IUTILS
#define INCLUDED_MOMSRVR_IUTILS

#ifndef INCLUDED_MOMSRVR_FORWARD
#include <momsrvr_forward.h>
#endif

// Local includes

// External includes

#ifndef INCLUDED_AIMCOMMON_OUTPARAM
#include <aimcommon_outparam.h>
#endif

#ifndef INCLUDED_BDLT_DATETZ
#include <bdlt_datetz.h>
#endif

namespace BloombergLP {
namespace momsrvr {

/**
 * An interface for external functionality required by momsrvr.
 *
 * @internal
 * This is primarily provided to allow unit testing and it is not expected to be derived.
 * @endinternal
 */
class IUtils {
public:
    /** Destructor. */
    virtual ~IUtils()
    {
        // Do nothing
    }

    // Note: side affect load FTDB
    virtual bool loadMomTicket(momtkt& ticket) const = 0;

    virtual bool loadParmSubset() const = 0;

    virtual int sendTktOut(momtkt& ticket) const = 0;

    virtual int sendToTradeFeed(momtkt& ticket, int checkTasu, int autoRelease) const = 0;

    virtual bool isFirmReleaseSet(momtkt& ticket) const = 0;

    virtual int convertDateFromIntToDateTz(const int dateAsInt,
                                           bdlt::DateTz& dateAsDateTz) const = 0;
};

} // namespace momsrvr
} // namespace BloombergLP

#endif
