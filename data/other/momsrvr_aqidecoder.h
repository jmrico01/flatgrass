#ifndef INCLUDED_MOMSRVR_AQIDECODER
#define INCLUDED_MOMSRVR_AQIDECODER

#include <amcat_response.h>
#include <amcsys_ticketdeletion.h>
#include <aqia_bastcpclientadapter.h>
#include <aqitu_bbcurrencyutil.h>
#include <aqitu_legacytradeutil.h>
#include <bcema_sharedptr.h>
#include <bdet_datetz.h>
#include <bdeut_variant.h>
#include <bdlt_dateutil.h>
#include <bsitzo_tzdftimezoneutil.h>
#include <bslma_allocator.h>
#include <bslma_managedptr.h>
#include <islon.h>
#include <mom_tkt.h>
#include <tmib_atlasmsgcontext.h>
#include <tmib_consumersession.h>
#include <tzdf_time_zones.h>

namespace BloombergLP {
namespace momsrvr {

class AQIDecoder {
public:
    typedef aqia::TcTicketDataRecord AQITcTicketDataRecord;
    typedef amcat::TradeReference AQITradeReference;
    typedef bsl::vector<AQITradeReference> AQITradeReferences;
    typedef AQITradeReferences::iterator AQITradeReferencesIt;
    typedef AQITradeReferences::const_iterator AQITradeReferencesCit;
    typedef amcat::TradeReferenceType AQITradeReferenceType;

    static const bsl::string NEW_TRADE;
    static const bsl::string AMEND_TRADE;
    static const bsl::string CANCEL_TRADE;
    static const bsl::string DELETE_TRADE;
    static const bsl::string DELETE_ALLOC;

    enum RCode { TICKET_SUCCESS = 0, TICKET_FAIL = 1, INVALID_MESSAGE = 2, INVALID_MOM_TICKET = 3 };

private:
    static bool convertTradeReferenceId(const bsl::string& referenceId, int& outParam);
    static bool convertTradeReferenceId(const bsl::string& referenceId, short& outParam);
    static bdlt::DatetimeTz convertUtcToLocal(bdlt::DatetimeTz utcDatetimeTZ);
    static bool isTicketPassedCompliance(const bsl::string& complianceStatus);
    static bool isRecordTypeSupported(const bsl::string& recordType);

public:
    static bool parseReference(const AQITcTicketDataRecord& tcTicketData, momtkt& tkt);
    static bool parseTerms(const AQITcTicketDataRecord& tcTicketData, momtkt& tkt);
    static bool parseTicketData(const AQITcTicketDataRecord& tcTicketData, momtkt& tkt);
    static bool loadBroker(const AQITcTicketDataRecord& tcTicketData, momtkt& tkt);
    static bool loadDepartment(const AQITcTicketDataRecord& tcTicketData, momtkt& tkt);
    static bool loadTicketNumber(const AQITcTicketDataRecord& tcTicketData, momtkt& tkt);
    static bool getIsFullyAllocated(const AQITcTicketDataRecord& tcTicketData);
    static bool getComplianceStatus(const AQITcTicketDataRecord& tcTicketData,
                                    bsl::string& complianceStatus);
    static bool getRecordType(const AQITcTicketDataRecord& tcTicketData, bsl::string& recordType);

    static RCode isTicketValidForMom(const AQITcTicketDataRecord& tcTicketData);
    static bool loadAQIToMomtkt(const AQITcTicketDataRecord& tcTicketData, momtkt& tkt);
    static RCode constructMomtkt(const AQITcTicketDataRecord& tcTicketData,
                                 const bsl::string& messageIntent, int prcnum, momtkt& tkt);
};

} // namespace momsrvr
} // namespace BloombergLP
#endif
