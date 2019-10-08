#ifndef INCLUDED_MOMSRVR_PXLIST
#define INCLUDED_MOMSRVR_PXLIST

#include <bsl_vector.h>
#include <bslmt_mutex.h>

namespace BloombergLP {
namespace momsrvr {

/**
 * Utils class to update list of pricing numbers per machine
 */
class PxList { // Do we want to make this an object MomsrvrPxList with property
               // px list? or something
public:
    /** Destructor. */
    virtual ~PxList()
    {
        // Do nothing
    }

    static bool isEmpty();
    static void initialize();
    static void addNewPxToList(int px);
    static void removePxFromList(int px);
    static void print();
    static bool isPxListUpdated();
    static void updatedPxListCaptured();

    static bslmt::Mutex updatedPxListMutex;
    static bsl::vector<int> pricingNumbers;

private:
    static bool d_pxListIsUpdated;
};

} // namespace momsrvr
} // namespace BloombergLP

#endif
