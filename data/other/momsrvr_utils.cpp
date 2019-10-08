#include <momsrvr_utils.h>

// Local includes

// External includes

#include <afmt_fmem_bit_api_bank1_chk.h>
#include <afmt_fmem_bit_api_bank2_chk.h>
#include <alloc_util.h>
#include <audit_tkt_audt.h>
#include <audt_event.h>
#include <audt_interface.h>
#include <bael_log.h>
#include <bdlt_date.h>
#include <bdlt_datetz.h>
#include <bdlt_dateutil.h>
#include <bitlib.h>
#include <bsl_ctime.h>
#include <bsl_sstream.h>
#include <cfrmf.h>
#include <corpsflg.h>
#include <frectyp.h>
#include <ftrndb.h>
#include <interbig.h>
#include <is_ftrntkt_ok_for_vcon.h>
#include <mom_tkt.h>
#include <momsetsvmsg_api.h>
#include <momtk_defs.h>
#include <momtk_remoteupd.h>
#include <newtarl_util.h>
#include <ok_release.h>
#include <ok_release_util.h>
#include <parmscommon_define_3.h>
#include <parmscommon_define_6.h>
#include <products.h>
#include <resftrn2_fortran_funcs.h>
#include <savftrn2_fortran_funcs.h>
#include <swap_tkt_util.h>
#include <tarl_utils.h>
#include <tctm_release_util.h>
#include <tctm_send_util.h>
#include <tctm_util.h>
#include <tsam_trdutil.h>
#include <tsam_utils.h>
#include <uuidtouser.h>

#include <bbit/201708/bbit_disable_legacy_vcon_audt.h> // BREG 296333
#include <bbit/201711/bbit_disable_legacy_tradefeed_audt.h> // BREG 301970
#include <bbit/201712/bbit_send_electronic_trades_to_vcon.h> // BREG 305658
#include <bbit/201804/bbit_enable_ctm_autorelease_in_tc_flow.h> // BREG 317032
#include <bbit/201807/bbit_use_actionsv_for_momsrvr.h> // BREG 326133

extern "C" {

#include <scus_fstsnd.h>
#include <scus_fstsndreq.h>

} // extern "C"

namespace BloombergLP {
namespace momsrvr {

Utils::Utils()
{
    // do nothing
}

Utils::Utils(bslma::ManagedPtr<actionsvclient::IClient> actionsvClient)
    : d_actionsvClient(actionsvClient)
{
    // do nothing
}

bool Utils::loadMomTicket(momtkt& tk) const
{
    double r8amt;

    if (tk.prcnum <= 0) {
        return false;
    }

    if (tctm_accftrn_idx7(tk.prcnum, tk.tktnum)) {
        return false;
    }

    tk.trandate = FTDATE;
    tk.tkttype = 1;
    tk.transTime = time(NULL);
    tk.curr = FTCURR;
    tk.dept = FTDEPT;
    tk.setloc = FTSETLOC;
    tk.cxlnum = FTCXLNUM;
    tk.settledate = FTSETDT;
    tk.trade_date = FTASOFDT;
    tk.ftflag2 = FTFLAG2;
    tk.buy_sell = FTBUYFLG;
    tk.scus_bank = FTCUSBNK;
    tk.price = FTPRICE;
    tk.product = getMOMProductType();

    if (FTDEPT == EQTY || FTDEPT == PRFD) {
        r8amt = FT8AMOUNT;
    } else if (FTDEPT == MTGE) {
        r8amt = FT8AMOUNT * FTFACTOR * 1000;
    } else {
        r8amt = FT8AMOUNT * 1000;
    }

    tk.amount = r8amt;
    tk.platform = FTPLATFORM;

    tarl_format_fundname(tk.prcnum, tk.account, sizeof(tk.account) - 1);
    tarl_format_broker(tk.broker, sizeof(tk.broker));

    return true;
}

bool Utils::loadParmSubset() const
{
    BAEL_LOG_SET_CATEGORY("M_MOMSRVR.MOMSRVR_BUSINESS.LOADPARMSUBSET");

    bbint32_t userno;
    if (FTUSERS[0] > 0) {
        userno = FTUSERS[0];
    } else if (FTUSERS[1] > 0) {
        userno = FTUSERS[1];
    } else if (FTUSERS[2] > 0) {
        userno = FTUSERS[2];
    } else {
        userno = FTUSERS[3];
    }

    bbint32_t uuid = 0;
    bbint32_t firm = 0;
    bbint32_t rcode = 0;

    bool isSuccessful(true);

    if (userno > 0) {
        rcode = uuidtouser(&uuid, &userno, &firm);
        if (rcode != 0) {
            BAEL_LOG_ERROR << "loadParmSubset() -> get uuid error: prcnum=" << FTNUMBER
                           << ", userno: " << userno << ", rc=" << rcode
                           << " error_type=get_uuid_error" << BAEL_LOG_END;
            isSuccessful = false;
        }
    } else {
        BAEL_LOG_ERROR << "loadParmSubset() -> cannot get userno: " << userno
                       << " ftusers: FTUSERS[0]: " << FTUSERS[0] << " FTUSERS[1]: " << FTUSERS[1]
                       << " FTUSERS[2]: " << FTUSERS[2] << " FTUSERS[3]: " << FTUSERS[3]
                       << " error_type=cannot_get_userno" << BAEL_LOG_END;
        isSuccessful = false;
    }

    if (!isSuccessful) {
        uuid = 0;
        firm = 0;
    }

    P3PRCNUM = FTNUMBER;
    P6UUID = uuid;
    P3FIRM = firm;
    BAEL_LOG_INFO << "loadParmSubset -> prcnum=" << P3PRCNUM << ", uuid: " << P6UUID
                  << ", firmid: " << P3FIRM << BAEL_LOG_END;

    return isSuccessful;
}

int Utils::sendTktOut(momtkt& tk) const
{
    BAEL_LOG_SET_CATEGORY("M_MOMSRVR.MOMSRVR_BUSINESS.SENDTKTOUT");
    // Reset auto step..will be set below

    tk.matchingstatus = 0;

    switch (tk.curr_state) {
    case MOMDB_BLOCK_STATE:
        BAEL_LOG_INFO << ":sendTktOut dbg: prcnum=" << tk.prcnum << " tktnum=" << tk.tktnum
                      << " action=BLOCK" << BAEL_LOG_END;
        return sendToTCTM(tk);

    case MOMDB_ALLOCATIONS_STATE:
        BAEL_LOG_INFO << ":sendTktOut dbg: prcnum=" << tk.prcnum << " tktnum=" << tk.tktnum
                      << " action=ALLOC" << BAEL_LOG_END;
        return sendToAlloc(tk);

    case MOMDB_TRADEFEED_STATE:
        BAEL_LOG_INFO << ":sendTktOut dbg: prcnum=" << tk.prcnum << " tktnum=" << tk.tktnum
                      << " action=TRADEFEED" << BAEL_LOG_END;
        return sendToTradeFeed(tk);

    case MOMDB_SETTLEMENT_STATE:
        BAEL_LOG_INFO << ":sendTktOut dbg: prcnum=" << tk.prcnum << " tktnum=" << tk.tktnum
                      << " action=CUSTODY" << BAEL_LOG_END;
        return sendToCustody(tk);

    default:
        BAEL_LOG_ERROR << ":sendTktOut Error: prcnum=" << tk.prcnum << " tktnum=" << tk.tktnum
                       << " action=unknown_state_[_" << tk.curr_state << "_]"
                       << " error_type=sendTktOut_failed" << BAEL_LOG_END;
        return 1;
    }
}

int Utils::sendToTradeFeed(momtkt& ticket) const
{
    BAEL_LOG_SET_CATEGORY("M_MOMSRVR.MOMSRVR_BUSINESS.SENDTOTRADEFEED");

    if (!afmt_fmem_bit_1_2_chk(ticket.prcnum)) {
        BAEL_LOG_INFO << "prcnum=" << ticket.prcnum << " sendToTradeFeed: TARL not available"
                      << BAEL_LOG_END;
        return 1;
    }

    if (!afmt_fmem_bit_2_16_chk(ticket.prcnum)) {
        BAEL_LOG_INFO << "Firm is not turned on TARL. prcnum=" << ticket.prcnum
                      << " tktnum=" << ticket.tktnum << BAEL_LOG_END;
        return 1;
    }

    if (bbit_use_actionsv_for_momsrvr__value()) {
        bbint32_t rcode;
        if (is_ticket_tarl_all_released(ticket.prcnum, ticket.tktnum, &rcode)) {
            return 0; // skip for the tickets that have already released
            BAEL_LOG_INFO << "Skip TradeFeed for prcnum=" << ticket.prcnum
                          << " tktnum=" << ticket.tktnum << BAEL_LOG_END;
        }
        rcode = d_actionsvClient->releaseTradefeed(ticket.tktnum, ticket.prcnum, P3FIRM, P6UUID,
                                                   actionsvclient::AUTO_RELEASE);
        if (rcode) {
            BAEL_LOG_ERROR << "actionsvclient returned a bad code"
                           << " rc=" << rcode << " prcnum=" << ticket.prcnum
                           << " tktnum=" << ticket.tktnum << " error_type=actionsvclient_bad_rcode"
                           << BAEL_LOG_END;
            return rcode;
        }
    } else {
        bbint32_t rcode = tctm_release_tickets(ticket.prcnum, ticket.tktnum, 0, "MOMSRVR");
        if (rcode) {
            BAEL_LOG_ERROR << "Error autorls TARL alloc.s for prcnum=" << ticket.prcnum
                           << " tktnum=" << ticket.tktnum << " error_type=sendToTradeFeed_failed"
                           << BAEL_LOG_END;
            return rcode;
        }
    }

    int rc = 0;
    short tarl_rls_ok = is_ticket_tarl_all_released(ticket.prcnum, ticket.tktnum, &rc);
    if (tarl_rls_ok == TRUE) {
        ticket.matchingstatus = 1;
        BAEL_LOG_INFO << "tarl all released. rc=" << tarl_rls_ok << " prcnum=" << ticket.prcnum
                      << " tktnum=" << ticket.tktnum << BAEL_LOG_END;
        return 0;
    } else {
        BAEL_LOG_INFO << "not tarl all released. rc=" << tarl_rls_ok << " prcnum=" << ticket.prcnum
                      << " tktnum=" << ticket.tktnum << BAEL_LOG_END;
        return MOM_STATE_IN_PROGRESS;
    }
}

bool Utils::isFirmReleaseSet(momtkt& ticket) const
{
    BAEL_LOG_SET_CATEGORY("M_MOMSRVR.MOMSRVR_BUSINESS.ISFIRMRELEASESET");

    BAEL_LOG_INFO << __func__ << ": checking firm release"
                  << " prcnum=" << ticket.prcnum << " tktnum=" << ticket.tktnum << BAEL_LOG_END;

    if (!afmt_fmem_bit_1_2_chk(ticket.prcnum)) {
        BAEL_LOG_INFO << "sendToTradeFeed: TARL not available"
                      << " prcnum=" << ticket.prcnum << " tktnum=" << ticket.tktnum << BAEL_LOG_END;
        return false;
    }

    if (!afmt_fmem_bit_2_16_chk(ticket.prcnum)) {
        BAEL_LOG_INFO << "Firm is not turned on TARL."
                      << " prcnum=" << ticket.prcnum << " tktnum=" << ticket.tktnum << BAEL_LOG_END;
        return false;
    }

    bbint32_t rcode = tctm_accftrn_idx7(ticket.prcnum, ticket.tktnum);
    if (rcode != 0) {
        BAEL_LOG_ERROR << "sendToTradeFeed: Accftrn error."
                       << " prcnum=" << ticket.prcnum << " tktnum=" << ticket.tktnum
                       << " rc=" << rcode << " error_type=sendToTradeFeed_failed" << BAEL_LOG_END;
        return false;
    }

    short tarl_rls_ok = 0;
    ok_release_w_args(&ticket.prcnum, &ticket.tktnum, &tarl_rls_ok, FALSE, TRUE);
    BAEL_LOG_INFO << __func__ << ": ok_release returned"
                  << " ok=" << tarl_rls_ok << " prcnum=" << ticket.prcnum
                  << " tktnum=" << ticket.tktnum << BAEL_LOG_END;

    return tarl_rls_ok != 0;
}

int Utils::sendToTradeFeed(momtkt& t, int check_tasu, int auto_rls) const
{
    BAEL_LOG_SET_CATEGORY("M_MOMSRVR.MOMSRVR_BUSINESS.SENDTOTRADEFEED");
    bbint32_t rcode;

    if (!afmt_fmem_bit_1_2_chk(t.prcnum)) {
        BAEL_LOG_INFO << "prcnum=" << t.prcnum << " sendToTradeFeed: TARL not available"
                      << BAEL_LOG_END;
        return 1;
    }

    if (!afmt_fmem_bit_2_16_chk(t.prcnum)) {
        BAEL_LOG_INFO << "Firm is not turned on TARL. prcnum=" << t.prcnum << " tktnum=" << t.tktnum
                      << BAEL_LOG_END;
        return 1;
    }

    /* load and release the master ticket */
    rcode = tctm_accftrn_idx7(t.prcnum, t.tktnum);
    if (rcode != 0) {
        BAEL_LOG_ERROR << "sendToTradeFeed:Accftrn error. prcnum=" << t.prcnum
                       << " tktnum=" << t.tktnum << " rc=" << rcode
                       << " error_type=sendToTradeFeed_failed" << BAEL_LOG_END;
        return rcode;
    }

    short tarl_rls_ok = 0;

    if (check_tasu) {
        BAEL_LOG_INFO << "tarl check tasu" << BAEL_LOG_END;
        ok_release_w_args(&t.prcnum, &t.tktnum, &tarl_rls_ok, FALSE, FALSE);
    }

    if (tarl_rls_ok || !check_tasu) {
        if (is_tkt_fully_allocated()) {
            if (bbit_use_actionsv_for_momsrvr__value()) {
                if (is_ticket_tarl_all_released(t.prcnum, t.tktnum, &rcode)) {
                    return 0; // skip for the tickets that have already released
                    BAEL_LOG_INFO << "Skip TradeFeed for prcnum=" << t.prcnum
                                  << " tktnum=" << t.tktnum << BAEL_LOG_END;
                }
                rcode = d_actionsvClient->releaseTradefeed(t.tktnum, t.prcnum, P3FIRM, P6UUID,
                                                           actionsvclient::AUTO_RELEASE);
                if (rcode) {
                    BAEL_LOG_ERROR << "actionsvclient returned a bad code"
                                   << " rc=" << rcode << " prcnum=" << t.prcnum
                                   << " tktnum=" << t.tktnum
                                   << " error_type=actionsvclient_bad_rcode" << BAEL_LOG_END;
                    return rcode;
                }
            } else {
                rcode = tctm_release_tickets(t.prcnum, t.tktnum, 0, "MOMSRVR");
                if (rcode) {
                    BAEL_LOG_ERROR << "Error autorls TARL alloc.s for prcnum=" << t.prcnum
                                   << " tktnum=" << t.tktnum << " error_type=sendToTradeFeed_failed"
                                   << BAEL_LOG_END;
                    return rcode;
                }
            }
        } else {
            BAEL_LOG_INFO << "ticket not fully allocated, not releasing to tradefeed"
                          << " prcnum=" << t.prcnum << " tktnum=" << t.tktnum << BAEL_LOG_END;
            return MOM_STATE_IN_PROGRESS;
        }
    }

    int rc = 0;
    tarl_rls_ok = is_ticket_tarl_all_released(t.prcnum, t.tktnum, &rc);

    if (tarl_rls_ok == TRUE) {
        if (!bbit_use_actionsv_for_momsrvr__value()
            && !bbit_disable_legacy_tradefeed_audt__value()) {
            auditTkt(MOMDB_TRADEFEED_STATE, t);
        }
        t.matchingstatus = 1;
        BAEL_LOG_INFO << "tarl all released. rc=" << tarl_rls_ok << " prcnum=" << t.prcnum
                      << " tktnum=" << t.tktnum << BAEL_LOG_END;
        return 0;
    } else {
        BAEL_LOG_INFO << "not tarl all released. rc=" << tarl_rls_ok << " prcnum=" << t.prcnum
                      << " tktnum=" << t.tktnum << BAEL_LOG_END;
        return MOM_STATE_IN_PROGRESS;
    }
}

int Utils::convertDateFromIntToDateTz(const int dateAsInt, bdlt::DateTz& dateAsDateTz) const
{
    bdlt::Date ticketDate;
    int rcode = bdlt::DateUtil::convertFromYYYYMMDD(&ticketDate, dateAsInt);

    if (rcode != 0) {
        return rcode;
    }

    dateAsDateTz = bdlt::DateTz(ticketDate, 0);
    return rcode;
}

void Utils::auditTkt(int step, momtkt& tk)
{
    BAEL_LOG_SET_CATEGORY("M_MOMSRVR.MOMSRVR_BUSINESS.AUDITTKT");
    bbint32_t audit_rcode = 0, audit_on = 0;
    bool audit_started = false;
    int save_func;
    string audit_event;

    if (audit_tkt_on_audt() == 1) {
        char audit_id[FTAUDT_ID_LEN + 1];
        cfrmf(audit_id, sizeof(audit_id), FTAUDT_ID, FTAUDT_ID_LEN);

        if (audit_tkt_valid_faudt_id_(audit_id) == 1) {
            audit_on = 1;

            // set PINDEX to MOM
            memcpy(&save_func, &INTAREA[12],
                   sizeof(int)); // save contents of INTAREA[12]
            memcpy(&INTAREA[12], "MOM",
                   sizeof(INTAREA[12])); // override contents

            switch (step) {
            case MOMDB_BLOCK_STATE:
                audit_event = TKTMOM_BLOCK;
                break;
            case MOMDB_ALLOCATIONS_STATE:
                audit_event = TKTMOM_ALLOC;
                break;
            case MOMDB_TRADEFEED_STATE:
                audit_event = TKTMOM_FEED;
                break;
            case MOMDB_SETTLEMENT_STATE:
                audit_event = TKTMOM_CUSTODY;
                break;
            default:
                return;
            }

            audit_start(AUDIT_APPL_ID_TKT, audit_id, audit_event.c_str(), (short)tk.prcnum,
                        &audit_rcode);

            if (audit_rcode != 0) {
                BAEL_LOG_ERROR << "audit_start() error: " << audit_rcode
                               << " for tktnum=" << tk.tktnum << " and prcnum=" << tk.prcnum
                               << " error_type=audit_start_failed" << BAEL_LOG_END;
                return;
            }
            audit_started = true;

            BAEL_LOG_INFO << "audit ok id " << audit_id << " for tktnum=" << tk.tktnum
                          << " and prcnum=" << tk.prcnum << BAEL_LOG_END;
        } else {
            BAEL_LOG_ERROR << "audit not valid id problem tktnum=" << tk.tktnum
                           << " prcnum=" << tk.prcnum << " error_type=audit_not_valid_id_problem"
                           << BAEL_LOG_END;
            return;
        }
    } else {
        BAEL_LOG_INFO << "audit not on for " << tk.prcnum << BAEL_LOG_END;
        return;
    }

    // try a dummy ftrn update for audt
    update_mom_release_bit();

    if (audit_on && audit_started) {
        audit_send(&audit_rcode);

        if (audit_rcode != 0) {
            audit_cancel((short)tk.prcnum, &audit_rcode);
        } else {
            audit_end();
        }

        memcpy(&INTAREA[12], &save_func, sizeof(INTAREA[12]));
    }

    return;
}

bool Utils::isSwapTicket()
{
    if (FTDEPT == CORP && FTSFLAG == CORP_SFLG_SWAP) {
        return true;
    } else {
        return false;
    }
}

// check MOM firm setting for tpr circle/final
// return rcode 1 if ftrpvariance == 0 and setting is on
// default for setting should be 0 (auto-release ftrpvariance 0)
bool Utils::isTPREligibleForRelease(momtkt& t)
{
    BAEL_LOG_SET_CATEGORY("M_MOMSRVR.MOMSRVR_BUSINESS.ISTPRELIGIBLEFORRELEASE");
    int rcode = 0;
    enum { Circle = 1, Final = 2 };

    string rvalue = "false"; // default value
    string function = "MOM";
    string category = "workflow";
    string key = "TPRReleaseFinalAllocs";
    ostringstream ostr;
    ostr << "tspx" << t.prcnum;
    string firm = ostr.str();
    string typedata = "";

    BAEL_LOG_INFO << "sendToAlloc: before get_mom_setting_call. " << firm << "-" << P6UUID
                  << BAEL_LOG_END;
    rcode = momsetsvmsg::get_mom_setting(124260, 1, 0, rvalue, function, category, key,
                                         MOM_SETTING_FIRM, typedata, 0, P6UUID, &firm);
    if (rcode) {
        BAEL_LOG_ERROR << "sendToAlloc: error getting mom setting "
                       << "for TPR Final Allocations. rc=" << rcode << " tktnum=" << t.tktnum
                       << " prcnum=" << t.prcnum << " error_type=sendToAlloc_failed"
                       << BAEL_LOG_END;
    } else {
        BAEL_LOG_INFO << "sendToAlloc: " << key << "=" << rvalue << " tktnum=" << t.tktnum
                      << " prcnum=" << t.prcnum << BAEL_LOG_END;
    }

    if (rvalue.compare("true") == 0 && FTRPVARIANCE != Final) {
        BAEL_LOG_INFO << "sendToAlloc: TPR allocations not eligible "
                      << "for auto-release. variance=" << FTRPVARIANCE << " tktnum=" << t.tktnum
                      << " prcnum=" << t.prcnum << BAEL_LOG_END;
        return false;
    }

    return true;
}

bool Utils::isVCONConfirmed()
{
    BAEL_LOG_SET_CATEGORY("M_MOMSRVR.MOMSRVR_BUSINESS.ISVCONCONFIRMED");
    bbint32_t sav_buf[FTRNDB_TOTSIZE_2];
    bbint32_t prcnum = FTNUMBER;
    bbint32_t relnum = FTRELNUM;
    bbint32_t oxftktnum = 0;
    bbint32_t searchTktnum = 0;
    bbint32_t rcode;

    BAEL_LOG_INFO << "isVCONConfirmed. tktnum=" << FTTKTNUM << "ftrectype: " << FTRECTYP
                  << BAEL_LOG_END;

    if (btst(FTFLAG12, 11)) {
        BAEL_LOG_INFO << "prcnum=" << FTNUMBER << " tktnum=" << FTTKTNUM
                      << " already confirmed on VCON" << BAEL_LOG_END;
        return true;
    } else if (FTRECTYP == PM) {
        // note: p3prcnum has to be loaded for this API
        get_swap_oxf_from_om(FTTKTNUM, &oxftktnum);
        searchTktnum = oxftktnum;
        BAEL_LOG_INFO << "prcnum=" << FTNUMBER << " OXF tktnum=" << oxftktnum << BAEL_LOG_END;

        if (searchTktnum == 0) {
            BAEL_LOG_INFO << "prcnum=" << FTNUMBER << " search ticket num not available"
                          << BAEL_LOG_END;
            return false;
        }

        savftrn2_(sav_buf);

        rcode = tctm_accftrn_idx7(prcnum, searchTktnum);
        if (rcode != 0) {
            BAEL_LOG_ERROR << "prcnum=" << prcnum << " tktnum=" << relnum
                           << " failed to load FTRN record. rc=" << rcode
                           << " error_type=loading_FTRN_record_failed" << BAEL_LOG_END;

            resftrn2_(sav_buf);
            return false;
        }

        if (btst(FTFLAG12, 11)) {
            BAEL_LOG_INFO << "prcnum=" << FTNUMBER << " tktnum=" << FTTKTNUM
                          << " already confirmed on VCON." << BAEL_LOG_END;
            resftrn2_(sav_buf);
            return true;
        }

        resftrn2_(sav_buf);
    }

    return false;
}

bool Utils::isAllocTkt(momtkt& tk)
{
    return btst(tk.ftflag2, 12);
}

int Utils::sendToAlloc(momtkt& t) const
{
    using namespace actionsvclient;
    BAEL_LOG_SET_CATEGORY("M_MOMSRVR.MOMSRVR_BUSINESS.SENDTOALLOC");

    BAEL_LOG_INFO << "tktnum=" << t.tktnum << " prcnum=" << t.prcnum << " facility=" << t.facility
                  << BAEL_LOG_END;

    int rcode = 0;
    bbint32_t loadftrn;
    if (isSwapTicket()) { // swap ticket, don't chck vcon confirm bit
        loadftrn = NO_VCON_CONF_CHK;
    } else {
        loadftrn = LOAD_FTRN;
    }

    enum { TPRepo = 14 };

    // check MOM firm setting for tpr circle/final
    // return rcode 1 if ftrpvariance == 0 and setting is on
    // default for setting should be 0 (auto-release ftrpvariance 0)

    if (TPRepo == t.product && !isTPREligibleForRelease(t)) {
        return 1;
    }

    switch (t.facility) {
    case ALLOC_DEST_BB:
    case ALLOC_DEST_BBTM:
    case ALLOC_DEST_VBB:
    case ALLOC_DEST_VBBTM:
    case ALLOC_DEST_OASYS:
    case ALLOC_DEST_VOMGEO:
    case ALLOC_DEST_VOASYSTM:
    case ALLOC_DEST_TWEB:
    case ALLOC_DEST_MAXESS:
    case ALLOC_DEST_TM:
    case ALLOC_DEST_VTM:
    case ALLOC_DEST_SWIFT:
    case ALLOC_DEST_ACCORD:
    case ALLOC_DEST_MISYS:
        if (bbit_use_actionsv_for_momsrvr__value()) {
            rcode = d_actionsvClient->releaseAllocation(t.tktnum, t.prcnum, P3FIRM, P6UUID,
                                                        actionsvclient::AUTO_RELEASE);
            // If not eligible for allocations auto release
            if (rcode == RCODE::ALLOCATIONS_NOT_ELIGIBLE_FOR_AUTO_RELEASE) {
                BAEL_LOG_INFO << "Skip action=ALLOC for "
                              << "prcnum=" << t.prcnum << " tktnum=" << t.tktnum << " not eligible."
                              << BAEL_LOG_END;
                t.matchingstatus = 1; // autoprocess next step
                rcode = RCODE::SUCCESS;
                return rcode;
            } else if (rcode) {
                BAEL_LOG_ERROR << "actionsvclient returned a bad code"
                               << " rc=" << rcode << " prcnum=" << t.prcnum
                               << " tktnum=" << t.tktnum << " error_type=actionsvclient_bad_rcode"
                               << BAEL_LOG_END;
            }
        } else {
            rcode = auto_release_block_alloc(t.prcnum, t.tktnum, loadftrn, 0, tsam_alloc_rls,
                                             t.facility);
            // If not eligible for allocations auto release
            if (rcode == -1) {
                BAEL_LOG_INFO << "Skip action=ALLOC for "
                              << "prcnum=" << t.prcnum << " tktnum=" << t.tktnum << " not eligible."
                              << BAEL_LOG_END;
                t.matchingstatus = 1; // autoprocess next step
                rcode = 0;
                return rcode;
            }
        }
        break;
    case ALLOC_DEST_CTM:
        if (bbit_enable_ctm_autorelease_in_tc_flow__value()) {
            if (t.tkttype == PM || (t.tkttype == TT && !isAllocTkt(t))) {
                BAEL_LOG_INFO << "Releasing ticket to CTM"
                              << " tktnum=" << t.tktnum << " prcnum=" << t.prcnum << BAEL_LOG_END;
                if (bbit_use_actionsv_for_momsrvr__value()) {
                    rcode = d_actionsvClient->releaseAllocation(t.tktnum, t.prcnum, P3FIRM, P6UUID,
                                                                actionsvclient::AUTO_RELEASE);
                } else {
                    rcode = autosend_add_to_tctmdb(t.prcnum, 23257, t.tktnum);
                }
                if (rcode == 0) { /* all good */
                    // Update FTDB flag that ticket is released to CTM
                    update_ftflag_release_bit();
                }
                break;
            }
        }
    default:
        // For now other facilities
        BAEL_LOG_INFO << "prcnum=" << t.prcnum << " tktnum=" << t.tktnum << " Facility "
                      << t.facility << " not eligible for alloc. auto release. Waiting in TSAM"
                      << BAEL_LOG_END;
        return rcode;
    }

    if (rcode == 0) {
        BAEL_LOG_INFO << "prcnum=" << t.prcnum << " tktnum=" << t.tktnum << " Facility "
                      << t.facility << " action=ALLOC completed" << BAEL_LOG_END;

        if (!bbit_use_actionsv_for_momsrvr__value()) {
            auditTkt(MOMDB_ALLOCATIONS_STATE, t);
        }
    } else if (rcode == -2) {
        BAEL_LOG_WARN << "prcnum=" << t.prcnum << " tktnum=" << t.tktnum
                      << " action=ALLOC could not relase. Did not pass validation. rc=" << rcode
                      << BAEL_LOG_END;
    } else {
        BAEL_LOG_ERROR << "prcnum=" << t.prcnum << " tktnum=" << t.tktnum
                       << " action=ALLOC release error. rc=" << rcode
                       << " error_type=ALLOC_release_error" << BAEL_LOG_END;
    }

    return rcode;
}
int Utils::sendToCustody(momtkt& t) const
{
    BAEL_LOG_SET_CATEGORY("M_MOMSRVR.MOMSRVR_BUSINESS.SENDTOCUSTODY");
    SCUS_BULK_REQUEST req;
    int rcode = 0;
    short tarl_rls_ok = 0;

    if (tctm_accftrn_idx7(t.prcnum, t.tktnum)) {
        BAEL_LOG_ERROR << "failed to load ftrn record prcnum=" << t.prcnum << " tktnum=" << t.tktnum
                       << " error_type=loading_FTRN_record_failed" << BAEL_LOG_END;
    }
    memset(&req, 0, sizeof(req));
    req.rtype = REQUEST_SINGLE_RELEASE;
    req.prcnum = t.prcnum;
    req.srchTktnum = t.tktnum;
    req.user = FTUSERS[0];

    /* if px is ON for TARL, check TARL
       release, otherwise go ! */
    int px = t.prcnum;

    if (afmt_fmem_bit_2_16_chk(px)) {
        tarl_rls_ok = is_ticket_tarl_all_released(t.prcnum, t.tktnum, &rcode);

        if (!tarl_rls_ok) {
            BAEL_LOG_INFO << "prcnum=" << t.prcnum << " tktnum=" << t.tktnum
                          << " not released to action=FEED"
                          << " -- prcnum=" << t.prcnum << " tktnum=" << t.tktnum
                          << " not sent to action=CUSTODY" << BAEL_LOG_END;
            return 1;
        }
    }

    if (bbit_use_actionsv_for_momsrvr__value()) {
        rcode = d_actionsvClient->sendToCustodian(t.tktnum, t.prcnum, P3FIRM, P6UUID,
                                                  actionsvclient::AUTO_RELEASE);
        if (rcode) {
            BAEL_LOG_ERROR << "actionsvclient returned a bad code"
                           << " rc=" << rcode << " prcnum=" << t.prcnum << " tktnum=" << t.tktnum
                           << " error_type=actionsvclient_bad_rcode" << BAEL_LOG_END;
        }
    } else {
        rcode = scus_bulk_fstsnd(&req);
        if (rcode == -1) {
            BAEL_LOG_ERROR << "Custody Offline is not up."
                           << " error_type=custody_offline_not_up"
                           << " tktnum=" << t.tktnum << " prcnum=" << t.prcnum << BAEL_LOG_END;
        } else if (rcode < 0) {
            BAEL_LOG_ERROR << "tktnum=" << t.tktnum << " prcnum=" << t.prcnum
                           << " sendToCustody failed. rc=" << rcode
                           << " error_type=sendToCustody_failed" << BAEL_LOG_END;
        } else {
            BAEL_LOG_INFO << "tktnum=" << t.tktnum << " prcnum=" << t.prcnum
                          << " sent to custody server" << BAEL_LOG_END;

            auditTkt(MOMDB_SETTLEMENT_STATE, t);
        }
    }

    return rcode;
}

int Utils::sendToTCTM(momtkt& t) const
{
    BAEL_LOG_SET_CATEGORY("M_MOMSRVR.MOMSRVR_BUSINESS.SENDTOTCTM");
#define TSAM_AUTO_SEND_TO_VCON 13
    int rcode = 0;
    short fac = static_cast<short>(t.facility);
    if (is_trade_dest_for_vcon(fac)) {
        // Skip VCON and move forward for ET tades (false)
        if (!bbit_send_electronic_trades_to_vcon__value()
            && (FALSE
                == is_ftrntkt_ok_for_vcon(FTAESEQ, FTAEBROK, FTAEAPPL, FTORDER_BIT_FLAGS,
                                          FTFLAG2))) {
            BAEL_LOG_INFO << "Skipping action=BLOCK for ET tkt. tktnum=" << t.tktnum
                          << " prcnum=" << t.prcnum << BAEL_LOG_END;
            t.matchingstatus = 1; // autoprocess next step
            return rcode;
        } else if (isSwapTicket() && isVCONConfirmed()) {
            BAEL_LOG_INFO << "Skipping action=BLOCK swap tkt already confirmed. tktnum=" << t.tktnum
                          << " prcnum=" << t.prcnum << BAEL_LOG_END;

            t.matchingstatus = 1; // autoprocess next step
            return rcode;
        } else {
            if (!btst(FTFLAG2, 2) && !btst(FTFLAG2, 12)) {
                if (is_tsam_auto_support_yk(FTDEPT)) {
                    BAEL_LOG_INFO << "sendToTCTM. tktnum=" << t.tktnum << " prcnum=" << t.prcnum
                                  << BAEL_LOG_END;
                    if (bbit_use_actionsv_for_momsrvr__value()) {
                        rcode = d_actionsvClient->releaseBlock(t.tktnum, t.prcnum, P3FIRM, P6UUID,
                                                               actionsvclient::AUTO_RELEASE);
                        if (rcode) {
                            BAEL_LOG_ERROR << "actionsvclient returned a bad code"
                                           << " rc=" << rcode << " prcnum=" << t.prcnum
                                           << " tktnum=" << t.tktnum
                                           << " error_type=actionsvclient_bad_rcode"
                                           << BAEL_LOG_END;
                            return rcode;
                        }
                    } else {
                        rcode = auto_release_block_alloc(t.prcnum, t.tktnum, 1, 0, tsam_block_rls,
                                                         t.facility);

                        if (rcode) {
                            BAEL_LOG_ERROR << "Auto send to VCON failed tktnum=" << t.tktnum
                                           << " prcnum=" << t.prcnum << " rc=" << rcode
                                           << " error_type=send_To_VCON_Failed" << BAEL_LOG_END;
                            return rcode;
                        }
                    }

                } else {
                    // Short-term fix, proceed to next steps.
                    // In the future, prevent use of VCON facilities in FX tickets.
                    BAEL_LOG_INFO << "Skipping action=BLOCK for ineligible product type."
                                  << " tktnum=" << t.tktnum << " prcnum=" << t.prcnum
                                  << BAEL_LOG_END;
                    t.matchingstatus = 1; // autoprocess next step
                    return rcode;
                }
            } else {
                BAEL_LOG_INFO << "Not VCON eligible tktnum=" << t.tktnum << " prcnum=" << t.prcnum
                              << BAEL_LOG_END;
                rcode = 5;
            }
        }
    } else {
        BAEL_LOG_INFO << "Facility " << t.facility << " not VCON in tkt. tktnum=" << t.tktnum
                      << " prcnum=" << t.prcnum << BAEL_LOG_END;
        t.matchingstatus = 1; // autoprocess next step
        return rcode;
    }

    if (!bbit_use_actionsv_for_momsrvr__value() && !bbit_disable_legacy_vcon_audt__value()
        && rcode == 0) {
        auditTkt(MOMDB_BLOCK_STATE, t);
    }

    return rcode;
}

} // namespace momsrvr
} // namespace BloombergLP