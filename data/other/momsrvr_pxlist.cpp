#include <bael_log.h>
#include <bsl_sstream.h>
#include <bsl_vector.h>
#include <bslmt_lockguard.h>
#include <momsrvr_pxlist.h>
#include <tssrbigutil_pnumutil.h>

namespace {
const char LOG_CATEGORY[] = "MOMSRVR.PXLIST";
}

namespace BloombergLP {
namespace momsrvr {

bslmt::Mutex PxList::updatedPxListMutex;
bsl::vector<int> PxList::pricingNumbers;

bool PxList::d_pxListIsUpdated = false;

bool PxList::isEmpty()
{
    return pricingNumbers.empty();
}

void PxList::initialize()
{
    BAEL_LOG_SET_CATEGORY(LOG_CATEGORY);

    pricingNumbers = tssrbigutil::PnumUtil::getPxonPnumList();
    BAEL_LOG_INFO << __func__ << ": Pricing numbers list size: " << pricingNumbers.size()
                  << BAEL_LOG_END;
}

void PxList::addNewPxToList(int prcnum)
{
    BAEL_LOG_SET_CATEGORY(LOG_CATEGORY);

    bslmt::LockGuard<bslmt::Mutex> lguard(&updatedPxListMutex);
    pricingNumbers.push_back(prcnum);

    BAEL_LOG_INFO << __func__ << ": Recieved PXON signal."
                  << " prcnum=" << prcnum
                  << ", pricing numbers list size updated to: " << pricingNumbers.size()
                  << BAEL_LOG_END;

    d_pxListIsUpdated = true;
}

void PxList::removePxFromList(int prcnum)
{
    BAEL_LOG_SET_CATEGORY(LOG_CATEGORY);

    bslmt::LockGuard<bslmt::Mutex> lguard(&updatedPxListMutex);
    bsl::vector<int>::iterator it = pricingNumbers.begin();
    while (it != pricingNumbers.end()) {
        if (*it == prcnum) {
            it = pricingNumbers.erase(it);
            BAEL_LOG_DEBUG << __func__ << ": Removed prcnum from pxList."
                           << " prcnum=" << prcnum << BAEL_LOG_END;
        } else
            it++;
    }

    BAEL_LOG_INFO << __func__ << ": Recieved PXOF signal."
                  << " prcnum=" << prcnum
                  << ", pricing numbers list size updated to: " << pricingNumbers.size()
                  << BAEL_LOG_END;

    d_pxListIsUpdated = true;
}

void PxList::print()
{
    BAEL_LOG_SET_CATEGORY(LOG_CATEGORY);
    int pxListLength = pricingNumbers.size();
    bsl::stringstream buffer;
    buffer << __func__ << "size=" << pxListLength << " ";

    buffer << "pxList=[";
    for (int i = 0; i < pxListLength; i++) {
        if (i != 0) {
            buffer << ",";
        }
        buffer << pricingNumbers[i];
    }
    buffer << "]" << bsl::endl;

    BAEL_LOG_INFO << buffer.str() << BAEL_LOG_END;
}

void PxList::updatedPxListCaptured()
{
    d_pxListIsUpdated = false;
}

bool PxList::isPxListUpdated()
{
    return d_pxListIsUpdated;
}

} // namespace momsrvr
} // namespace BloombergLP
