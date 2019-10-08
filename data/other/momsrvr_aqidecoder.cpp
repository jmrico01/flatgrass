#include <momsrvr_aqidecoder.h>

#include <boost/lexical_cast.hpp>
namespace BloombergLP {
namespace momsrvr {

namespace {
typedef bsl::vector<bsl::string> RecordTypes;

const bsl::string COMPLIANCE_STATUS = "COMPLIANCE_STATUS";
const bsl::string COMPLIANCE_PASSED = "PASSED";
const bsl::string COMPLIANCE_APPROVED = "APPROVED";
const bsl::string COMPLIANCE_UNDEFINED = "UNDEFINED";

const bsl::string RECORDTYPE_LIST[] = { "CPN", "DIV", "MTD", "CSH" };
static const RecordTypes UNSUPPORTED_RECORD_TYPES(
    RECORDTYPE_LIST, RECORDTYPE_LIST + sizeof(RECORDTYPE_LIST) / sizeof(RECORDTYPE_LIST[0]));
} // namespace

const bsl::string AQIDecoder::NEW_TRADE = "NEW_TRADE";
const bsl::string AQIDecoder::AMEND_TRADE = "AMEND_TRADE";
const bsl::string AQIDecoder::CANCEL_TRADE = "CANCEL_TRADE";
const bsl::string AQIDecoder::DELETE_TRADE = "DELETE_TRADE";
const bsl::string AQIDecoder::DELETE_ALLOC = "DELETE_ALLOC";

bool AQIDecoder::convertTradeReferenceId(const bsl::string& referenceId, int& out)
{
    BAEL_LOG_SET_CATEGORY("MOMSRVR.AQIDECODER.CONVERTTRADEREFERENCEID");
    try {
        out = boost::lexical_cast<int>(referenceId.c_str());
    } catch (const boost::bad_lexical_cast& e) {
        BAEL_LOG_ERROR << " Failed to cast referenceId to int, error=" << e.what()
                       << " error_type=error_cast_referenceId" << BAEL_LOG_END;
        return false;
    }
    return true;
}

bool AQIDecoder::convertTradeReferenceId(const bsl::string& referenceId, short& out)
{
    BAEL_LOG_SET_CATEGORY("MOMSRVR.AQIDECODER.CONVERTTRADEREFERENCEID");
    try {
        out = boost::lexical_cast<short>(referenceId.c_str());
    } catch (const boost::bad_lexical_cast& e) {
        BAEL_LOG_ERROR << " Failed to cast referenceId to short, error=" << e.what()
                       << " error_type=error_cast_referenceId" << BAEL_LOG_END;
        return false;
    }
    return true;
}

bdlt::DatetimeTz AQIDecoder::convertUtcToLocal(bdlt::DatetimeTz utcDatetimeTZ)
{
    BAEL_LOG_SET_CATEGORY("MOMSRVR.AQIDECODER.CONVERTUTCTOLOCAL");

    int machineTZDF = 0;

    if (isny()) {
        machineTZDF = TZDF_EASTERN_13_M5;
    } else if (islon()) {
        machineTZDF = TZDF_LONDON_22_0;
    } else if (istokyo()) {
        machineTZDF = TZDF_TOKYO_41_P9;
    }

    const char* zoneId = 0;
    bool ignoreDstFlag = false;
    int rcode = 0;

    /* Get Zone Id */
    rcode = bsitzo_TzdfTimeZoneUtil::getTimeZoneId(&zoneId, &ignoreDstFlag, machineTZDF);
    if (0 != rcode) {
        BAEL_LOG_WARN << " Error getting zoneId string :"
                      << " rc=" << rcode << " tz=" << machineTZDF << BAEL_LOG_END;

        return utcDatetimeTZ;
    }

    bdlt::DatetimeTz retVal;
    rcode = bsitzo_TzdfTimeZoneUtil::convertUtcToLocalTime(&retVal, zoneId, ignoreDstFlag,
                                                           utcDatetimeTZ.utcDatetime());

    if (0 != rcode) {
        BAEL_LOG_WARN << " Error converting UTC to Local DateTime :"
                      << " rc=" << rcode << " tz=" << machineTZDF << " zoneId=" << zoneId
                      << " ignoreDstFlag=" << ignoreDstFlag << " utcDatetimeTZ=" << utcDatetimeTZ
                      << BAEL_LOG_END;

        return utcDatetimeTZ;
    }

    return retVal;
}

bool AQIDecoder::isTicketPassedCompliance(const bsl::string& complianceStatus)
{
    return (complianceStatus == COMPLIANCE_UNDEFINED || complianceStatus == COMPLIANCE_PASSED
            || complianceStatus == COMPLIANCE_APPROVED);
}

bool AQIDecoder::isRecordTypeSupported(const bsl::string& recordType)
{
    return bsl::find(UNSUPPORTED_RECORD_TYPES.begin(), UNSUPPORTED_RECORD_TYPES.end(), recordType)
        == UNSUPPORTED_RECORD_TYPES.end();
}

bool AQIDecoder::parseReference(const AQITcTicketDataRecord& tcTicketData, momtkt& tkt)
{
    BAEL_LOG_SET_CATEGORY("MOMSRVR.AQIDECODER.PARSEREFERENCE");

    const AQITradeReferences& tradeReferences
        = tcTicketData.legacyTrade().tradeIdentifier().references();

    for (AQITradeReferencesCit cit = tradeReferences.cbegin(); cit != tradeReferences.cend();
         ++cit) {
        switch (cit->type()) {
        case amcat::TradeReferenceType::PARENT_LEGACY_BLOCK_ID:
        case amcat::TradeReferenceType::PARENT_LEGACY_ALLOC_ID:
            convertTradeReferenceId(cit->id(), tkt.relnum);
            BAEL_LOG_DEBUG << " Received TradeReferenceType relnum=" << tkt.relnum << BAEL_LOG_END;
            break;
        case amcat::TradeReferenceType::CXL_LEGACY_BLOCK_ID:
        case amcat::TradeReferenceType::CXL_LEGACY_ALLOC_ID:
            convertTradeReferenceId(cit->id(), tkt.cxlnum);
            BAEL_LOG_DEBUG << " Received TradeReferenceType cxlnum=" << tkt.cxlnum << BAEL_LOG_END;
            break;
        case amcat::TradeReferenceType::AEX_SEQ_ID:
            convertTradeReferenceId(cit->id(), tkt.ftaeseq);
            BAEL_LOG_DEBUG << " Received TradeReferenceType ftaeseq=" << tkt.ftaeseq
                           << BAEL_LOG_END;
            break;
        case amcat::TradeReferenceType::AEX_BRKR_ID:
            convertTradeReferenceId(cit->id(), tkt.ftaebrok);
            BAEL_LOG_DEBUG << " Received TradeReferenceType ftaebrok=" << tkt.ftaebrok
                           << BAEL_LOG_END;
            break;
        case amcat::TradeReferenceType::AEX_APPL_ID:
            convertTradeReferenceId(cit->id(), tkt.ftaeappl);
            BAEL_LOG_DEBUG << " Received TradeReferenceType ftaeappl= " << tkt.ftaeappl
                           << BAEL_LOG_END;
            break;
        default:
            BAEL_LOG_DEBUG << " Unsupported TradeReferenceType: " << cit->type() << BAEL_LOG_END;
            break;
        }
    }
    // TODO: handle case when correction has no cxlnum, currently momsrvr
    // doesn't use cxlnum
    return true;
}

bool AQIDecoder::parseTerms(const AQITcTicketDataRecord& tcTicketData, momtkt& tkt)
{
    BAEL_LOG_SET_CATEGORY("MOMSRVR.AQIDECODER.PARSETERMS");

    if (tcTicketData.legacyTrade().terms().isNull()) {
        BAEL_LOG_ERROR << " TradeTerms is empty"
                       << " error_type=missing_field_TradeTerms" << BAEL_LOG_END;
        return false;
    }

    const amcat::TradeTerms& terms = tcTicketData.legacyTrade().terms().value();

    switch (terms.direction()) {
    case amcat::TradeDirection::BUY_TO_OPEN:
    case amcat::TradeDirection::BUY_TO_CLOSE:
    case amcat::TradeDirection::RECEIVE:
        tkt.buy_sell = 0;
        break;
    case amcat::TradeDirection::SELL_TO_OPEN:
    case amcat::TradeDirection::SELL_TO_CLOSE:
    case amcat::TradeDirection::PAY:
        tkt.buy_sell = 1;
        break;
    default:
        BAEL_LOG_DEBUG << " Unsupported TradeDirection: " << terms.direction() << BAEL_LOG_END;
        break;
    }

    bdlt::DatetimeTz tradeDatetime = convertUtcToLocal(terms.entryDateTime());
    tkt.trandate = bdlt::DateUtil::convertToYYYYMMDD(tradeDatetime.localDatetime().date());

    bdlt::DatetimeTz asOfDatetime = convertUtcToLocal(terms.asofDateTime());
    tkt.trade_date = bdlt::DateUtil::convertToYYYYMMDD(asOfDatetime.localDatetime().date());

    bdlt::DateTz settlementDate = bdet_DateTz(terms.settleDate(), 0);
    tkt.settledate = bdlt::DateUtil::convertToYYYYMMDD(settlementDate.localDate());

    tkt.amount = terms.quantity().value();
    tkt.price = terms.price().value();
    tkt.setloc = terms.settlementLocationRefId().value();

    const bsl::string currencyStr = terms.cost().totalCost().currency().tradeCurrency();
    const char* bbCurrencyCd;
    short ftcurr;
    if (int rcode = aqitu::BBCurrencyUtil::getBBCurrency(&bbCurrencyCd, &ftcurr, currencyStr)) {
        BAEL_LOG_ERROR << " Failed to get currency number for currency=" << currencyStr
                       << " rcode=" << rcode << " error_type=failed_get_currency" << BAEL_LOG_END;
        return false;
    }
    tkt.curr = ftcurr;

    return true;
}

bool AQIDecoder::parseTicketData(const AQITcTicketDataRecord& tcTicketData, momtkt& tkt)
{
    BAEL_LOG_SET_CATEGORY("MOMSRVR.AQIDECODER.PARSETICKETDATA");

    if (tcTicketData.legacyTrade().ticketData().isNull()) {
        BAEL_LOG_ERROR << " TradeTicketData is empty"
                       << " error_type=missing_field_TradeTicketData" << BAEL_LOG_END;
        return false;
    }

    const amcat::TradeTicketData& ticketData = tcTicketData.legacyTrade().ticketData().value();
    bsl::string rectype;
    if (!getRecordType(tcTicketData, rectype)) {
        return false;
    }
    strncpy(tkt.rectype, rectype.c_str(), sizeof(tkt.rectype));

    if (ticketData.executionVenueRefId().isNull()) {
        BAEL_LOG_ERROR << " executionVenueRefId is empty"
                       << " error_type=missing_field_executionVenueRefId" << BAEL_LOG_END;
        return false;
    }
    tkt.platform = ticketData.executionVenueRefId().value();

    return true;
}

bool AQIDecoder::getRecordType(const AQITcTicketDataRecord& tcTicketData, bsl::string& recordType)
{
    BAEL_LOG_SET_CATEGORY("MOMSRVR.AQIDECODER.GETRECORDTYPE");

    if (tcTicketData.legacyTrade().ticketData().isNull()) {
        BAEL_LOG_ERROR << " TradeTicketData is empty"
                       << " error_type=missing_field_TradeTicketData" << BAEL_LOG_END;
        return false;
    }

    const amcat::TradeTicketData& ticketData = tcTicketData.legacyTrade().ticketData().value();
    if (ticketData.legacyRecordTypeRefId().isNull()) {
        BAEL_LOG_ERROR << " legacyRecordTypeRefId is empty"
                       << " error_type=missing_field_legacyRecordTypeRefId" << BAEL_LOG_END;
        return false;
    }

    recordType = ticketData.legacyRecordTypeRefId().value();
    return true;
}

bool AQIDecoder::loadBroker(const AQITcTicketDataRecord& tcTicketData, momtkt& tkt)
{
    BAEL_LOG_SET_CATEGORY("MOMSRVR.AQIDECODER.LOADBROKER");

    for (bsl::vector<amcat::TradePartyReferenceRole>::const_iterator cit
         = tcTicketData.legacyTrade().partyRole().cbegin();
         cit != tcTicketData.legacyTrade().partyRole().cend(); ++cit) {
        switch (cit->role()) {
        case amcat::PartyTradeRole::BROKER:
        case amcat::PartyTradeRole::BROKER_AGENCY:
        case amcat::PartyTradeRole::BROKER_PRINCIPAL:
        case amcat::PartyTradeRole::RISKLESS_BROKER:
            if (cit->reference().type() == amcat::ValueType::STRING) {
                strncpy(tkt.broker, cit->reference().id().c_str(), sizeof(tkt.broker));
                return true;
            }
            break;
        default:
            BAEL_LOG_DEBUG << " Unsupported PartyTradeRole: " << cit->role() << BAEL_LOG_END;
            break;
        }
    }
    return false;
}

bool AQIDecoder::loadDepartment(const AQITcTicketDataRecord& tcTicketData, momtkt& tkt)
{
    BAEL_LOG_SET_CATEGORY("MOMSRVR.AQIDECODER.LOADDEPARTMENT");

    if (tcTicketData.legacyTrade().product().isNull()
        || tcTicketData.legacyTrade().product().value().legacyId().isNull()) {
        BAEL_LOG_ERROR << " tcTicketData product is empty, error_type=missing_field_tcTicketData"
                       << " tktnum=" << tkt.tktnum << " prcnum=" << tkt.prcnum << BAEL_LOG_END;
        return false;
    }

    tkt.dept = tcTicketData.legacyTrade().product().value().legacyId().value().department();
    return true;
}

bool AQIDecoder::loadTicketNumber(const AQITcTicketDataRecord& tcTicketData, momtkt& tkt)
{
    BAEL_LOG_SET_CATEGORY("MOMSRVR.AQIDECODER.LOADTICKETNUMBER");
    const AQITradeReferences& tradeReferences
        = tcTicketData.legacyTrade().tradeIdentifier().references();

    for (AQITradeReferencesCit cit = tradeReferences.cbegin(); cit != tradeReferences.cend();
         ++cit) {
        if (cit->type() == amcat::TradeReferenceType::LEGACY_BLOCK_ID
            || cit->type() == amcat::TradeReferenceType::LEGACY_ALLOC_ID) {
            return convertTradeReferenceId(cit->id(), tkt.tktnum);
        }
    }
    return false;
}

bool AQIDecoder::getIsFullyAllocated(const AQITcTicketDataRecord& tcTicketData)
{
    return aqitu::LegacyTradeUtil::isFullyAllocated(tcTicketData.legacyTrade());
}

bool AQIDecoder::getComplianceStatus(const AQITcTicketDataRecord& tcTicketData,
                                     bsl::string& complianceStatus)
{
    bsl::vector<amcat::TradeProcessingState> processingStates
        = tcTicketData.legacyTrade().processingState();
    for (bsl::vector<amcat::TradeProcessingState>::const_iterator cit = processingStates.cbegin();
         cit != processingStates.cend(); ++cit) {
        if (cit->type() == COMPLIANCE_STATUS) {
            complianceStatus = cit->code();
            return true;
        }
    }
    return false;
}

AQIDecoder::RCode AQIDecoder::isTicketValidForMom(const AQITcTicketDataRecord& tcTicketData)
{
    BAEL_LOG_SET_CATEGORY("MOMSRVR.AQIDECODER.ISTICKETVALIDFORMOM");

    bsl::string recordType;
    if (!getRecordType(tcTicketData, recordType)) {
        return TICKET_FAIL;
    }

    if (!isRecordTypeSupported(recordType)) {
        BAEL_LOG_DEBUG << "Record type is not supported, rectype=" << recordType << BAEL_LOG_END;
        return INVALID_MOM_TICKET;
    }

    bsl::string complianceStatus;
    if (!getComplianceStatus(tcTicketData, complianceStatus)) {
        return TICKET_FAIL;
    }

    if (!isTicketPassedCompliance(complianceStatus)) {
        BAEL_LOG_DEBUG << "Ticket did not pass compliance, complianceStatus=" << complianceStatus
                       << BAEL_LOG_END;
        return INVALID_MOM_TICKET;
    }

    return TICKET_SUCCESS;
}

bool AQIDecoder::loadAQIToMomtkt(const AQITcTicketDataRecord& tcTicketData, momtkt& tkt)
{
    BAEL_LOG_SET_CATEGORY("MOMSRVR.AQIDECODER.LOADAQITOMOMTKT");

    // Retrieve tktnum/relnum/cxlnum/ftaeseq/ftaebrok/ftaeappl
    if (!parseReference(tcTicketData, tkt)) {
        BAEL_LOG_ERROR << " Failed to parse reference, error_type=failed_parse_reference"
                       << " prcnum=" << tkt.prcnum << BAEL_LOG_END;
        return false;
    }

    // Retrieve buy_sell/trandate/amount/price/trandate/trade_date/settledate
    if (!parseTerms(tcTicketData, tkt)) {
        BAEL_LOG_ERROR << " Failed to parse terms, error_type=failed_parse_terms"
                       << " tktnum=" << tkt.tktnum << " prcnum=" << tkt.prcnum << BAEL_LOG_END;
        return false;
    }

    // Retrieve rectype/platform
    if (!parseTicketData(tcTicketData, tkt)) {
        BAEL_LOG_ERROR << " Failed to parse ticketData, error_type=failed_parse_ticketData"
                       << " tktnum=" << tkt.tktnum << " prcnum=" << tkt.prcnum << BAEL_LOG_END;
        return false;
    }

    // Retrieve broker
    if (!loadBroker(tcTicketData, tkt)) {
        BAEL_LOG_ERROR << " Failed to load broker, error_type=failed_load_broker"
                       << " tktnum=" << tkt.tktnum << " prcnum=" << tkt.prcnum << BAEL_LOG_END;
        return false;
    }

    // Retrieve dept
    if (!loadDepartment(tcTicketData, tkt)) {
        BAEL_LOG_ERROR << " Failed to load department, error_type=failed_load_department"
                       << " tktnum=" << tkt.tktnum << " prcnum=" << tkt.prcnum << BAEL_LOG_END;
        return false;
    }

    return true;
}

AQIDecoder::RCode AQIDecoder::constructMomtkt(const AQITcTicketDataRecord& tcTicketData,
                                              const bsl::string& messageIntent, int prcnum,
                                              momtkt& tkt)
{
    BAEL_LOG_SET_CATEGORY("MOMSRVR.AQIDECODER.CONSTRUCTMOMTKT");

    bool isFullyAllocated = getIsFullyAllocated(tcTicketData);

    tkt.prcnum = prcnum;
    if (!loadTicketNumber(tcTicketData, tkt)) {
        return TICKET_FAIL;
    }

    AQIDecoder::RCode rcode;
    rcode = isTicketValidForMom(tcTicketData);
    if (rcode) {
        BAEL_LOG_INFO << " Ticket not going to be processed, rcode=" << rcode
                      << " tktnum=" << tkt.tktnum << " prcnum=" << tkt.prcnum << BAEL_LOG_END;
        return rcode;
    }

    if (messageIntent == AQIDecoder::NEW_TRADE) {
        tkt.tktaction = ADD_TKT;
    } else if (messageIntent == AQIDecoder::AMEND_TRADE && isFullyAllocated) {
        tkt.tktaction = XPT_ALLOC_UPD;
    } else {
        BAEL_LOG_DEBUG << " Unable to get ticket a valid action for messageIntent=" << messageIntent
                       << " isFullyAllocated=" << isFullyAllocated << BAEL_LOG_END;
        return INVALID_MESSAGE;
    }

    BAEL_LOG_DEBUG << " Populating ticket fields for tktnum=" << tkt.tktnum
                   << " prcnum=" << tkt.prcnum << BAEL_LOG_END;
    if (!loadAQIToMomtkt(tcTicketData, tkt)) {
        return TICKET_FAIL;
    }

    BAEL_LOG_INFO << " Parsed tkt fields: ["
                  << " tktnum=" << tkt.tktnum << " prcnum=" << tkt.prcnum
                  << " trandate=" << tkt.trandate << " tktaction=" << tkt.tktaction
                  << " curr=" << tkt.curr << " dept=" << tkt.dept << " setloc=" << tkt.setloc
                  << " price=" << tkt.price << " amount=" << tkt.amount
                  << " buy_sell=" << tkt.buy_sell << " broker=" << tkt.broker
                  << " rectype=" << tkt.rectype << " ftaeseq=" << tkt.ftaeseq
                  << " ftaebrok=" << tkt.ftaebrok << " ftaeappl=" << tkt.ftaeappl
                  << " settledate=" << tkt.settledate << " trade_date=" << tkt.trade_date
                  << " cxlnum=" << tkt.cxlnum << " relnum=" << tkt.relnum
                  << " platform=" << tkt.platform << " fullyAllocated=" << isFullyAllocated << " ]"
                  << BAEL_LOG_END;

    return TICKET_SUCCESS;
}
} // namespace momsrvr
} // namespace BloombergLP
