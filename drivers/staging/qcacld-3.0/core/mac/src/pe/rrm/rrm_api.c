/*
 * Copyright (c) 2012-2018 The Linux Foundation. All rights reserved.
 *
 * Permission to use, copy, modify, and/or distribute this software for
 * any purpose with or without fee is hereby granted, provided that the
 * above copyright notice and this permission notice appear in all
 * copies.
 *
 * THE SOFTWARE IS PROVIDED "AS IS" AND THE AUTHOR DISCLAIMS ALL
 * WARRANTIES WITH REGARD TO THIS SOFTWARE INCLUDING ALL IMPLIED
 * WARRANTIES OF MERCHANTABILITY AND FITNESS. IN NO EVENT SHALL THE
 * AUTHOR BE LIABLE FOR ANY SPECIAL, DIRECT, INDIRECT, OR CONSEQUENTIAL
 * DAMAGES OR ANY DAMAGES WHATSOEVER RESULTING FROM LOSS OF USE, DATA OR
 * PROFITS, WHETHER IN AN ACTION OF CONTRACT, NEGLIGENCE OR OTHER
 * TORTIOUS ACTION, ARISING OUT OF OR IN CONNECTION WITH THE USE OR
 * PERFORMANCE OF THIS SOFTWARE.
 */

/**=========================================================================

   \file  rrm_api.c

   \brief implementation for PE RRM APIs

   ========================================================================*/

/* $Header$ */


/*--------------------------------------------------------------------------
   Include Files
   ------------------------------------------------------------------------*/
#include "cds_api.h"
#include "wni_api.h"
#include "sir_api.h"
#include "ani_global.h"
#include "wni_cfg.h"
#include "lim_types.h"
#include "lim_utils.h"
#include "lim_send_sme_rsp_messages.h"
#include "parser_api.h"
#include "lim_send_messages.h"
#include "rrm_global.h"
#include "rrm_api.h"

#define MAX_RRM_TX_PWR_CAP 22

uint8_t
rrm_get_min_of_max_tx_power(tpAniSirGlobal pMac,
			    int8_t regMax, int8_t apTxPower)
{
	uint8_t maxTxPower = 0;
	uint8_t txPower = QDF_MIN(regMax, (apTxPower));

	if ((txPower >= RRM_MIN_TX_PWR_CAP) && (txPower <= RRM_MAX_TX_PWR_CAP))
		maxTxPower = txPower;
	else if (txPower < RRM_MIN_TX_PWR_CAP)
		maxTxPower = RRM_MIN_TX_PWR_CAP;
	else
		maxTxPower = RRM_MAX_TX_PWR_CAP;

	pe_debug("regulatoryMax: %d, apTxPwr: %d, maxTxpwr: %d",
		regMax, apTxPower, maxTxPower);
	return maxTxPower;
}

/* -------------------------------------------------------------------- */
/**
 * rrm_cache_mgmt_tx_power
 **
 * FUNCTION:  Store Tx power for management frames.
 *
 * LOGIC:
 *
 * ASSUMPTIONS:
 *
 * NOTE:
 *
 * @param pSessionEntry session entry.
 * @return None
 */
void
rrm_cache_mgmt_tx_power(tpAniSirGlobal pMac, int8_t txPower,
			tpPESession pSessionEntry)
{
	pe_debug("Cache Mgmt Tx Power: %d", txPower);

	if (pSessionEntry == NULL)
		pMac->rrm.rrmPEContext.txMgmtPower = txPower;
	else
		pSessionEntry->txMgmtPower = txPower;
}

/* -------------------------------------------------------------------- */
/**
 * rrm_get_mgmt_tx_power
 *
 * FUNCTION:  Get the Tx power for management frames.
 *
 * LOGIC:
 *
 * ASSUMPTIONS:
 *
 * NOTE:
 *
 * @param pSessionEntry session entry.
 * @return txPower
 */
int8_t rrm_get_mgmt_tx_power(tpAniSirGlobal pMac, tpPESession pSessionEntry)
{
	if (pSessionEntry == NULL)
		return pMac->rrm.rrmPEContext.txMgmtPower;

	pe_debug("tx mgmt pwr %d", pSessionEntry->txMgmtPower);

	return pSessionEntry->txMgmtPower;
}

/* -------------------------------------------------------------------- */
/**
 * rrm_send_set_max_tx_power_req
 *
 * FUNCTION:  Send WMA_SET_MAX_TX_POWER_REQ message to change the max tx power.
 *
 * LOGIC:
 *
 * ASSUMPTIONS:
 *
 * NOTE:
 *
 * @param txPower txPower to be set.
 * @param pSessionEntry session entry.
 * @return None
 */
tSirRetStatus
rrm_send_set_max_tx_power_req(tpAniSirGlobal pMac, int8_t txPower,
			      tpPESession pSessionEntry)
{
	tpMaxTxPowerParams pMaxTxParams;
	tSirRetStatus retCode = eSIR_SUCCESS;
	tSirMsgQ msgQ;

	if (pSessionEntry == NULL) {
		pe_err("Invalid parameters");
		return eSIR_FAILURE;
	}
	pMaxTxParams = qdf_mem_malloc(sizeof(tMaxTxPowerParams));
	if (NULL == pMaxTxParams) {
		pe_err("Unable to allocate memory for pMaxTxParams");
		return eSIR_MEM_ALLOC_FAILED;

	}
	/* Allocated memory for pMaxTxParams...will be freed in other module */
	pMaxTxParams->power = txPower;
	qdf_mem_copy(pMaxTxParams->bssId.bytes, pSessionEntry->bssId,
		     QDF_MAC_ADDR_SIZE);
	qdf_mem_copy(pMaxTxParams->selfStaMacAddr.bytes,
			pSessionEntry->selfMacAddr,
			QDF_MAC_ADDR_SIZE);

	msgQ.type = WMA_SET_MAX_TX_POWER_REQ;
	msgQ.reserved = 0;
	msgQ.bodyptr = pMaxTxParams;
	msgQ.bodyval = 0;

	pe_debug("Sending WMA_SET_MAX_TX_POWER_REQ with power(%d) to HAL",
		txPower);

	retCode = wma_post_ctrl_msg(pMac, &msgQ);
	if (eSIR_SUCCESS != retCode) {
		pe_err("Posting WMA_SET_MAX_TX_POWER_REQ to HAL failed, reason=%X",
			retCode);
		qdf_mem_free(pMaxTxParams);
		return retCode;
	}
	return retCode;
}

/* -------------------------------------------------------------------- */
/**
 * rrm_set_max_tx_power_rsp
 *
 * FUNCTION:  Process WMA_SET_MAX_TX_POWER_RSP message.
 *
 * LOGIC:
 *
 * ASSUMPTIONS:
 *
 * NOTE:
 *
 * @param txPower txPower to be set.
 * @param pSessionEntry session entry.
 * @return None
 */
tSirRetStatus rrm_set_max_tx_power_rsp(tpAniSirGlobal pMac, tpSirMsgQ limMsgQ)
{
	tSirRetStatus retCode = eSIR_SUCCESS;
	tpMaxTxPowerParams pMaxTxParams = (tpMaxTxPowerParams) limMsgQ->bodyptr;
	tpPESession pSessionEntry;
	uint8_t sessionId, i;

	if (qdf_is_macaddr_broadcast(&pMaxTxParams->bssId)) {
		for (i = 0; i < pMac->lim.maxBssId; i++) {
			if (pMac->lim.gpSession[i].valid == true) {
				pSessionEntry = &pMac->lim.gpSession[i];
				rrm_cache_mgmt_tx_power(pMac, pMaxTxParams->power,
							pSessionEntry);
			}
		}
	} else {
		pSessionEntry = pe_find_session_by_bssid(pMac,
							 pMaxTxParams->bssId.bytes,
							 &sessionId);
		if (pSessionEntry == NULL) {
			retCode = eSIR_FAILURE;
		} else {
			rrm_cache_mgmt_tx_power(pMac, pMaxTxParams->power,
						pSessionEntry);
		}
	}

	qdf_mem_free(limMsgQ->bodyptr);
	limMsgQ->bodyptr = NULL;
	return retCode;
}

/* -------------------------------------------------------------------- */
/**
 * rrm_process_link_measurement_request
 *
 * FUNCTION:  Processes the Link measurement request and send the report.
 *
 * LOGIC:
 *
 * ASSUMPTIONS:
 *
 * NOTE:
 *
 * @param pBd pointer to BD to extract RSSI and SNR
 * @param pLinkReq pointer to the Link request frame structure.
 * @param pSessionEntry session entry.
 * @return None
 */
tSirRetStatus
rrm_process_link_measurement_request(tpAniSirGlobal pMac,
				     uint8_t *pRxPacketInfo,
				     tDot11fLinkMeasurementRequest *pLinkReq,
				     tpPESession pSessionEntry)
{
	tSirMacLinkReport LinkReport;
	tpSirMacMgmtHdr pHdr;
	int8_t currentRSSI = 0;

	pe_debug("Received Link measurement request");

	if (pRxPacketInfo == NULL || pLinkReq == NULL || pSessionEntry == NULL) {
		pe_err("Invalid parameters - Ignoring the request");
		return eSIR_FAILURE;
	}
	pHdr = WMA_GET_RX_MAC_HEADER(pRxPacketInfo);

	LinkReport.txPower = lim_get_max_tx_power(pSessionEntry->def_max_tx_pwr,
						pLinkReq->MaxTxPower.maxTxPower,
						  pMac->roam.configParam.
						  nTxPowerCap);

	if ((LinkReport.txPower != (uint8_t) (pSessionEntry->maxTxPower)) &&
	    (eSIR_SUCCESS == rrm_send_set_max_tx_power_req(pMac,
							   LinkReport.txPower,
							   pSessionEntry))) {
		pe_warn("maxTx power in link report is not same as local..."
			" Local: %d Link Request TxPower: %d"
			" Link Report TxPower: %d",
			pSessionEntry->maxTxPower, LinkReport.txPower,
			pLinkReq->MaxTxPower.maxTxPower);
		pSessionEntry->maxTxPower =
			LinkReport.txPower;
	}

	LinkReport.dialogToken = pLinkReq->DialogToken.token;
	LinkReport.rxAntenna = 0;
	LinkReport.txAntenna = 0;
	currentRSSI = WMA_GET_RX_RSSI_RAW(pRxPacketInfo);

	pe_debug("Received Link report frame with %d", currentRSSI);

	/* 2008 11k spec reference: 18.4.8.5 RCPI Measurement */
	if ((currentRSSI) <= RCPI_LOW_RSSI_VALUE)
		LinkReport.rcpi = 0;
	else if ((currentRSSI > RCPI_LOW_RSSI_VALUE) && (currentRSSI <= 0))
		LinkReport.rcpi = CALCULATE_RCPI(currentRSSI);
	else
		LinkReport.rcpi = RCPI_MAX_VALUE;

	LinkReport.rsni = WMA_GET_RX_SNR(pRxPacketInfo);

	pe_debug("Sending Link report frame");

	return lim_send_link_report_action_frame(pMac, &LinkReport, pHdr->sa,
						 pSessionEntry);
}

/* -------------------------------------------------------------------- */
/**
 * rrm_process_neighbor_report_response
 *
 * FUNCTION:  Processes the Neighbor Report response from the peer AP.
 *
 * LOGIC:
 *
 * ASSUMPTIONS:
 *
 * NOTE:
 *
 * @param pNeighborRep pointer to the Neighbor report frame structure.
 * @param pSessionEntry session entry.
 * @return None
 */
tSirRetStatus
rrm_process_neighbor_report_response(tpAniSirGlobal pMac,
				     tDot11fNeighborReportResponse *pNeighborRep,
				     tpPESession pSessionEntry)
{
	tSirRetStatus status = eSIR_FAILURE;
	tpSirNeighborReportInd pSmeNeighborRpt = NULL;
	uint16_t length;
	uint8_t i;
	tSirMsgQ mmhMsg;

	if (pNeighborRep == NULL || pSessionEntry == NULL) {
		pe_err("Invalid parameters");
		return status;
	}

	pe_debug("Neighbor report response received");

	/* Dialog token */
	if (pMac->rrm.rrmPEContext.DialogToken !=
	    pNeighborRep->DialogToken.token) {
		pe_err("Dialog token mismatch in the received Neighbor report");
		return eSIR_FAILURE;
	}
	if (pNeighborRep->num_NeighborReport == 0) {
		pe_err("No neighbor report in the frame...Dropping it");
		return eSIR_FAILURE;
	}
	pe_debug("RRM:received num neighbor reports: %d",
			pNeighborRep->num_NeighborReport);
	if (pNeighborRep->num_NeighborReport > MAX_SUPPORTED_NEIGHBOR_RPT)
		pNeighborRep->num_NeighborReport = MAX_SUPPORTED_NEIGHBOR_RPT;
	length = (sizeof(tSirNeighborReportInd)) +
		 (sizeof(tSirNeighborBssDescription) *
		  (pNeighborRep->num_NeighborReport - 1));

	/* Prepare the request to send to SME. */
	pSmeNeighborRpt = qdf_mem_malloc(length);
	if (NULL == pSmeNeighborRpt) {
		pe_err("Unable to allocate memory");
		return eSIR_MEM_ALLOC_FAILED;
	}

	/* Allocated memory for pSmeNeighborRpt...will be freed by other module */

	for (i = 0; i < pNeighborRep->num_NeighborReport; i++) {
		pSmeNeighborRpt->sNeighborBssDescription[i].length = sizeof(tSirNeighborBssDescription);        /*+ any optional ies */
		qdf_mem_copy(pSmeNeighborRpt->sNeighborBssDescription[i].bssId,
			     pNeighborRep->NeighborReport[i].bssid,
			     sizeof(tSirMacAddr));
		pSmeNeighborRpt->sNeighborBssDescription[i].bssidInfo.rrmInfo.
		fApPreauthReachable =
			pNeighborRep->NeighborReport[i].APReachability;
		pSmeNeighborRpt->sNeighborBssDescription[i].bssidInfo.rrmInfo.
		fSameSecurityMode =
			pNeighborRep->NeighborReport[i].Security;
		pSmeNeighborRpt->sNeighborBssDescription[i].bssidInfo.rrmInfo.
		fSameAuthenticator =
			pNeighborRep->NeighborReport[i].KeyScope;
		pSmeNeighborRpt->sNeighborBssDescription[i].bssidInfo.rrmInfo.
		fCapSpectrumMeasurement =
			pNeighborRep->NeighborReport[i].SpecMgmtCap;
		pSmeNeighborRpt->sNeighborBssDescription[i].bssidInfo.rrmInfo.
		fCapQos = pNeighborRep->NeighborReport[i].QosCap;
		pSmeNeighborRpt->sNeighborBssDescription[i].bssidInfo.rrmInfo.
		fCapApsd = pNeighborRep->NeighborReport[i].apsd;
		pSmeNeighborRpt->sNeighborBssDescription[i].bssidInfo.rrmInfo.
		fCapRadioMeasurement = pNeighborRep->NeighborReport[i].rrm;
		pSmeNeighborRpt->sNeighborBssDescription[i].bssidInfo.rrmInfo.
		fCapDelayedBlockAck =
			pNeighborRep->NeighborReport[i].DelayedBA;
		pSmeNeighborRpt->sNeighborBssDescription[i].bssidInfo.rrmInfo.
		fCapImmediateBlockAck =
			pNeighborRep->NeighborReport[i].ImmBA;
		pSmeNeighborRpt->sNeighborBssDescription[i].bssidInfo.rrmInfo.
		fMobilityDomain =
			pNeighborRep->NeighborReport[i].MobilityDomain;

		pSmeNeighborRpt->sNeighborBssDescription[i].regClass =
			pNeighborRep->NeighborReport[i].regulatoryClass;
		pSmeNeighborRpt->sNeighborBssDescription[i].channel =
			pNeighborRep->NeighborReport[i].channel;
		pSmeNeighborRpt->sNeighborBssDescription[i].phyType =
			pNeighborRep->NeighborReport[i].PhyType;
	}

	pSmeNeighborRpt->messageType = eWNI_SME_NEIGHBOR_REPORT_IND;
	pSmeNeighborRpt->length = length;
	pSmeNeighborRpt->sessionId = pSessionEntry->smeSessionId;
	pSmeNeighborRpt->numNeighborReports = pNeighborRep->num_NeighborReport;
	qdf_mem_copy(pSmeNeighborRpt->bssId, pSessionEntry->bssId,
		     sizeof(tSirMacAddr));

	/* Send request to SME. */
	mmhMsg.type = pSmeNeighborRpt->messageType;
	mmhMsg.bodyptr = pSmeNeighborRpt;
	status = lim_sys_process_mmh_msg_api(pMac, &mmhMsg, ePROT);

	return status;

}

/* -------------------------------------------------------------------- */
/**
 * rrm_process_neighbor_report_req
 *
 * FUNCTION:
 *
 * LOGIC: Create a Neighbor report request and send it to peer.
 *
 * ASSUMPTIONS:
 *
 * NOTE:
 *
 * @param pNeighborReq Neighbor report request params .
 * @return None
 */
tSirRetStatus
rrm_process_neighbor_report_req(tpAniSirGlobal pMac,
				tpSirNeighborReportReqInd pNeighborReq)
{
	tSirRetStatus status = eSIR_SUCCESS;
	tSirMacNeighborReportReq NeighborReportReq;
	tpPESession pSessionEntry;
	uint8_t sessionId;

	if (pNeighborReq == NULL) {
		pe_err("NeighborReq is NULL");
		return eSIR_FAILURE;
	}
	pSessionEntry = pe_find_session_by_bssid(pMac, pNeighborReq->bssId,
						 &sessionId);
	if (pSessionEntry == NULL) {
		pe_err("session does not exist for given bssId");
		return eSIR_FAILURE;
	}

	pe_debug("SSID present: %d", pNeighborReq->noSSID);

	qdf_mem_set(&NeighborReportReq, sizeof(tSirMacNeighborReportReq), 0);

	NeighborReportReq.dialogToken = ++pMac->rrm.rrmPEContext.DialogToken;
	NeighborReportReq.ssid_present = !pNeighborReq->noSSID;
	if (NeighborReportReq.ssid_present) {
		qdf_mem_copy(&NeighborReportReq.ssid, &pNeighborReq->ucSSID,
			     sizeof(tSirMacSSid));
		QDF_TRACE_HEX_DUMP(QDF_MODULE_ID_PE,
				   QDF_TRACE_LEVEL_DEBUG,
				   (uint8_t *) NeighborReportReq.ssid.ssId,
				   NeighborReportReq.ssid.length);
	}

	status =
		lim_send_neighbor_report_request_frame(pMac, &NeighborReportReq,
						       pNeighborReq->bssId,
						       pSessionEntry);

	return status;
}

#define ABS(x)      ((x < 0) ? -x : x)
/* -------------------------------------------------------------------- */
/**
 * rrm_process_beacon_report_req
 *
 * FUNCTION:  Processes the Beacon report request from the peer AP.
 *
 * LOGIC:
 *
 * ASSUMPTIONS:
 *
 * NOTE:
 *
 * @param pCurrentReq pointer to the current Req comtext.
 * @param pBeaconReq pointer to the beacon report request IE from the peer.
 * @param pSessionEntry session entry.
 * @return None
 */
static tRrmRetStatus
rrm_process_beacon_report_req(tpAniSirGlobal pMac,
			      tpRRMReq pCurrentReq,
			      tDot11fIEMeasurementRequest *pBeaconReq,
			      tpPESession pSessionEntry)
{
	tSirMsgQ mmhMsg;
	tpSirBeaconReportReqInd pSmeBcnReportReq;
	uint8_t num_channels = 0, num_APChanReport;
	uint16_t measDuration, maxMeasduration;
	int8_t maxDuration;
	uint8_t sign;

	if (pBeaconReq->measurement_request.Beacon.BeaconReporting.present &&
	    (pBeaconReq->measurement_request.Beacon.BeaconReporting.
	     reportingCondition != 0)) {
		/* Repeated measurement is not supported. This means number of repetitions should be zero.(Already checked) */
		/* All test case in VoWifi(as of version 0.36)  use zero for number of repetitions. */
		/* Beacon reporting should not be included in request if number of repetitons is zero. */
		/* IEEE Std 802.11k-2008 Table 7-29g and section 11.10.8.1 */

		pe_err("Dropping the request: Reporting condition included in beacon report request and it is not zero");
		return eRRM_INCAPABLE;
	}

	/* The logic here is to check the measurement duration passed in the beacon request. Following are the cases handled.
	   Case 1: If measurement duration received in the beacon request is greater than the max measurement duration advertised
	   in the RRM capabilities(Assoc Req), and Duration Mandatory bit is set to 1, REFUSE the beacon request
	   Case 2: If measurement duration received in the beacon request is greater than the max measurement duration advertised
	   in the RRM capabilities(Assoc Req), and Duration Mandatory bit is set to 0, perform measurement for
	   the duration advertised in the RRM capabilities

	   maxMeasurementDuration = 2^(nonOperatingChanMax - 4) * BeaconInterval
	 */
	maxDuration =
		pMac->rrm.rrmPEContext.rrmEnabledCaps.nonOperatingChanMax - 4;
	sign = (maxDuration < 0) ? 1 : 0;
	maxDuration = (1L << ABS(maxDuration));
	if (!sign)
		maxMeasduration =
			maxDuration * pSessionEntry->beaconParams.beaconInterval;
	else
		maxMeasduration =
			pSessionEntry->beaconParams.beaconInterval / maxDuration;

	measDuration = pBeaconReq->measurement_request.Beacon.meas_duration;

	pe_debug("maxDuration = %d sign = %d maxMeasduration = %d measDuration = %d",
		maxDuration, sign, maxMeasduration, measDuration);

	if (maxMeasduration < measDuration) {
		if (pBeaconReq->durationMandatory) {
			pe_err("Dropping the request: duration mandatory and maxduration > measduration");
			return eRRM_REFUSED;
		} else
			measDuration = maxMeasduration;
	}
	/* Cache the data required for sending report. */
	pCurrentReq->request.Beacon.reportingDetail =
		pBeaconReq->measurement_request.Beacon.BcnReportingDetail.
		present ? pBeaconReq->measurement_request.Beacon.BcnReportingDetail.
		reportingDetail : BEACON_REPORTING_DETAIL_ALL_FF_IE;

	if (pBeaconReq->measurement_request.Beacon.
	    last_beacon_report_indication.present) {
		pCurrentReq->request.Beacon.last_beacon_report_indication =
			pBeaconReq->measurement_request.Beacon.
			last_beacon_report_indication.last_fragment;
		pe_debug("Last Beacon Report in request = %d",
			pCurrentReq->request.Beacon.
			last_beacon_report_indication);
	} else {
		pCurrentReq->request.Beacon.last_beacon_report_indication = 0;
		pe_debug("Last Beacon report not present in request");
	}

	if (pBeaconReq->measurement_request.Beacon.RequestedInfo.present) {
		if (!pBeaconReq->measurement_request.Beacon.RequestedInfo.
		    num_requested_eids) {
			pe_debug("802.11k BCN RPT: Requested num of EID is 0");
			return eRRM_FAILURE;
		}
		pCurrentReq->request.Beacon.reqIes.pElementIds =
			qdf_mem_malloc(sizeof(uint8_t) *
				       pBeaconReq->measurement_request.Beacon.
				       RequestedInfo.num_requested_eids);
		if (NULL == pCurrentReq->request.Beacon.reqIes.pElementIds) {
			pe_err("Unable to allocate memory for request IEs buffer");
			return eRRM_FAILURE;
		}

		pCurrentReq->request.Beacon.reqIes.num =
			pBeaconReq->measurement_request.Beacon.RequestedInfo.
			num_requested_eids;
		qdf_mem_copy(pCurrentReq->request.Beacon.reqIes.pElementIds,
			     pBeaconReq->measurement_request.Beacon.
			     RequestedInfo.requested_eids,
			     pCurrentReq->request.Beacon.reqIes.num);
		pe_debug("802.11k BCN RPT: Requested EIDs: num:[%d]",
			 pCurrentReq->request.Beacon.reqIes.num);
		QDF_TRACE_HEX_DUMP(QDF_MODULE_ID_PE, QDF_TRACE_LEVEL_DEBUG,
				pCurrentReq->request.Beacon.reqIes.pElementIds,
				pCurrentReq->request.Beacon.reqIes.num);
	}

	if (pBeaconReq->measurement_request.Beacon.num_APChannelReport) {
		for (num_APChanReport = 0;
		     num_APChanReport <
		     pBeaconReq->measurement_request.Beacon.num_APChannelReport;
		     num_APChanReport++)
			num_channels +=
				pBeaconReq->measurement_request.Beacon.
				APChannelReport[num_APChanReport].num_channelList;
	}
	/* Prepare the request to send to SME. */
	pSmeBcnReportReq = qdf_mem_malloc(sizeof(tSirBeaconReportReqInd));
	if (NULL == pSmeBcnReportReq) {
		pe_err("Unable to allocate memory during Beacon Report Req Ind to SME");
		return eRRM_FAILURE;

	}

	/* Allocated memory for pSmeBcnReportReq....will be freed by other modulea */
	qdf_mem_copy(pSmeBcnReportReq->bssId, pSessionEntry->bssId,
		     sizeof(tSirMacAddr));
	pSmeBcnReportReq->messageType = eWNI_SME_BEACON_REPORT_REQ_IND;
	pSmeBcnReportReq->length = sizeof(tSirBeaconReportReqInd);
	pSmeBcnReportReq->uDialogToken = pBeaconReq->measurement_token;
	pSmeBcnReportReq->msgSource = eRRM_MSG_SOURCE_11K;
	pSmeBcnReportReq->randomizationInterval =
		SYS_TU_TO_MS(pBeaconReq->measurement_request.Beacon.randomization);
	pSmeBcnReportReq->channelInfo.regulatoryClass =
		pBeaconReq->measurement_request.Beacon.regClass;
	pSmeBcnReportReq->channelInfo.channelNum =
		pBeaconReq->measurement_request.Beacon.channel;
	pSmeBcnReportReq->measurementDuration[0] = SYS_TU_TO_MS(measDuration);
	pSmeBcnReportReq->fMeasurementtype[0] =
		pBeaconReq->measurement_request.Beacon.meas_mode;
	qdf_mem_copy(pSmeBcnReportReq->macaddrBssid,
		     pBeaconReq->measurement_request.Beacon.BSSID,
		     sizeof(tSirMacAddr));

	if (pBeaconReq->measurement_request.Beacon.SSID.present) {
		pSmeBcnReportReq->ssId.length =
			pBeaconReq->measurement_request.Beacon.SSID.num_ssid;
		qdf_mem_copy(pSmeBcnReportReq->ssId.ssId,
			     pBeaconReq->measurement_request.Beacon.SSID.ssid,
			     pSmeBcnReportReq->ssId.length);
	}

	pCurrentReq->token = pBeaconReq->measurement_token;

	pSmeBcnReportReq->channelList.numChannels = num_channels;
	if (pBeaconReq->measurement_request.Beacon.num_APChannelReport) {
		uint8_t *ch_lst = pSmeBcnReportReq->channelList.channelNumber;
		uint8_t len;
		uint16_t ch_ctr = 0;

		for (num_APChanReport = 0;
		     num_APChanReport <
		     pBeaconReq->measurement_request.Beacon.num_APChannelReport;
		     num_APChanReport++) {
			len = pBeaconReq->measurement_request.Beacon.
			    APChannelReport[num_APChanReport].num_channelList;
			if (ch_ctr + len >
			   sizeof(pSmeBcnReportReq->channelList.channelNumber))
				break;

			qdf_mem_copy(&ch_lst[ch_ctr],
				     pBeaconReq->measurement_request.Beacon.
				     APChannelReport[num_APChanReport].
				     channelList, len);

			ch_ctr += len;
		}
	}
	/* Send request to SME. */
	mmhMsg.type = eWNI_SME_BEACON_REPORT_REQ_IND;
	mmhMsg.bodyptr = pSmeBcnReportReq;
	if (eSIR_SUCCESS != lim_sys_process_mmh_msg_api(pMac, &mmhMsg, ePROT))
		return eRRM_FAILURE;
	return eRRM_SUCCESS;
}

/* -------------------------------------------------------------------- */
/**
 * rrm_fill_beacon_ies
 *
 * FUNCTION:
 *
 * LOGIC: Fills Fixed fields and Ies in bss description to an array of uint8_t.
 *
 * ASSUMPTIONS:
 *
 * NOTE:
 *
 * @param pIes - pointer to the buffer that should be populated with ies.
 * @param pNumIes - returns the num of ies filled in this param.
 * @param pIesMaxSize - Max size of the buffer pIes.
 * @param eids - pointer to array of eids. If NULL, all ies will be populated.
 * @param numEids - number of elements in array eids.
 * @start_offset: Offset from where the IEs in the bss_desc should be parsed
 * @param pBssDesc - pointer to Bss Description.
 *
 * Returns: Remaining length of IEs in current bss_desc which are not included
 *	    in pIes.
 */
static uint8_t
rrm_fill_beacon_ies(tpAniSirGlobal pMac,
		    uint8_t *pIes, uint8_t *pNumIes, uint8_t pIesMaxSize,
		    uint8_t *eids, uint8_t numEids, uint8_t start_offset,
		    tpSirBssDescription pBssDesc)
{
	uint8_t len, *pBcnIes, count = 0, i;
	uint16_t BcnNumIes, total_ies_len;
	uint8_t rem_len = 0;

	if ((pIes == NULL) || (pNumIes == NULL) || (pBssDesc == NULL)) {
		pe_err("Invalid parameters");
		return 0;
	}
	/* Make sure that if eid is null, numEids is set to zero. */
	numEids = (eids == NULL) ? 0 : numEids;

	total_ies_len = GET_IE_LEN_IN_BSS(pBssDesc->length);
	BcnNumIes = total_ies_len;
	if (start_offset > BcnNumIes) {
		pe_err("Invalid start offset %d Bcn IE len %d",
		       start_offset, total_ies_len);
		return 0;
	}

	pBcnIes = (uint8_t *) &pBssDesc->ieFields[0];
	pBcnIes += start_offset;
	BcnNumIes = BcnNumIes - start_offset;

	*pNumIes = 0;

	/*
	 * If start_offset is 0, this is the first fragment of the current
	 * beacon. Include the Beacon Fixed Fields of length 12 bytes
	 * (BEACON_FRAME_IES_OFFSET) in the first fragment.
	 */
	if (start_offset == 0) {
		*((uint32_t *)pIes) = pBssDesc->timeStamp[0];
		*pNumIes += sizeof(uint32_t);
		pIes += sizeof(uint32_t);
		*((uint32_t *)pIes) = pBssDesc->timeStamp[1];
		*pNumIes += sizeof(uint32_t);
		pIes += sizeof(uint32_t);
		*((uint16_t *)pIes) = pBssDesc->beaconInterval;
		*pNumIes += sizeof(uint16_t);
		pIes += sizeof(uint16_t);
		*((uint16_t *)pIes) = pBssDesc->capabilityInfo;
		*pNumIes += sizeof(uint16_t);
		pIes += sizeof(uint16_t);
	}

	while (BcnNumIes > 0) {
		len = *(pBcnIes + 1) + 2;       /* element id + length. */
		pe_debug("EID = %d, len = %d total = %d",
			*pBcnIes, *(pBcnIes + 1), len);

		if (!len) {
			pe_err("Invalid length");
			break;
		}

		i = 0;
		do {
			if ((!eids) || (*pBcnIes == eids[i])) {
				if (((*pNumIes) + len) < pIesMaxSize) {
					pe_debug("Adding Eid %d, len=%d",
						 *pBcnIes, len);

					qdf_mem_copy(pIes, pBcnIes, len);
					pIes += len;
					*pNumIes += len;
					count++;
				} else {
					/*
					 * If max size of fragment is reached,
					 * calculate the remaining length and
					 * break. For first fragment, account
					 * for the fixed fields also.
					 */
					rem_len = total_ies_len - *pNumIes;
					if (start_offset == 0)
						rem_len = rem_len +
						BEACON_FRAME_IES_OFFSET;
					pe_debug("rem_len %d ies added %d",
						 rem_len, *pNumIes);
				}
				break;
			}
			i++;
		} while (i < numEids);

		if (rem_len)
			break;

		pBcnIes += len;
		BcnNumIes -= len;
	}
	pe_debug("Total length of Ies added = %d rem_len %d",
		 *pNumIes, rem_len);

	return rem_len;
}

/**
 * rrm_process_beacon_report_xmit() - create a rrm action frame
 * @mac_ctx: Global pointer to MAC context
 * @beacon_xmit_ind: Data for beacon report IE from SME.
 *
 * Create a Radio measurement report action frame and send it to peer.
 *
 * Return: tSirRetStatus
 */
tSirRetStatus
rrm_process_beacon_report_xmit(tpAniSirGlobal mac_ctx,
			       tpSirBeaconReportXmitInd beacon_xmit_ind)
{
	tSirRetStatus status = eSIR_SUCCESS;
	tSirMacRadioMeasureReport *report = NULL;
	tSirMacBeaconReport *beacon_report;
	tpSirBssDescription bss_desc;
	tpRRMReq curr_req = mac_ctx->rrm.rrmPEContext.pCurrentReq;
	tpPESession session_entry;
	uint8_t session_id, counter;
	uint8_t i, j, offset = 0;
	uint8_t bss_desc_count = 0;
	uint8_t report_index = 0;
	uint8_t rem_len = 0;
	uint8_t frag_id = 0;
	uint8_t num_frames, num_reports_in_frame;

	pe_debug("Received beacon report xmit indication");

	if (NULL == beacon_xmit_ind) {
		pe_err("Received beacon_xmit_ind is NULL in PE");
		return eSIR_FAILURE;
	}

	if (NULL == curr_req) {
		pe_err("Received report xmit while there is no request pending in PE");
		status = eSIR_FAILURE;
		goto end;
	}

	if ((beacon_xmit_ind->numBssDesc) || curr_req->sendEmptyBcnRpt) {
		beacon_xmit_ind->numBssDesc = (beacon_xmit_ind->numBssDesc ==
			RRM_BCN_RPT_NO_BSS_INFO) ? RRM_BCN_RPT_MIN_RPT :
			beacon_xmit_ind->numBssDesc;

		session_entry = pe_find_session_by_bssid(mac_ctx,
				beacon_xmit_ind->bssId, &session_id);
		if (NULL == session_entry) {
			pe_err("session does not exist for given bssId");
			status = eSIR_FAILURE;
			goto end;
		}

		report = qdf_mem_malloc(MAX_BEACON_REPORTS * sizeof(*report));

		if (NULL == report) {
			pe_err("RRM Report is NULL, allocation failed");
			status = eSIR_MEM_ALLOC_FAILED;
			goto end;
		}

		for (i = 0; i < MAX_BEACON_REPORTS &&
		     bss_desc_count < beacon_xmit_ind->numBssDesc; i++) {
			beacon_report =
				&report[i].report.beaconReport;
			/*
			 * If the scan result is NULL then send report request
			 * with option subelement as NULL.
			 */
			pe_debug("report %d bss %d", i, bss_desc_count);
			bss_desc = beacon_xmit_ind->
				   pBssDescription[bss_desc_count];
			/* Prepare the beacon report and send it to the peer.*/
			report[i].token =
				beacon_xmit_ind->uDialogToken;
			report[i].refused = 0;
			report[i].incapable = 0;
			report[i].type = SIR_MAC_RRM_BEACON_TYPE;

			/*
			 * Valid response is included if the size of
			 * becon xmit is == size of beacon xmit ind + ies
			 */
			if (beacon_xmit_ind->length < sizeof(*beacon_xmit_ind))
				continue;
			beacon_report->regClass = beacon_xmit_ind->regClass;
			if (bss_desc) {
				beacon_report->channel = bss_desc->channelId;
				qdf_mem_copy(beacon_report->measStartTime,
					bss_desc->startTSF,
					sizeof(bss_desc->startTSF));
				beacon_report->measDuration =
					SYS_MS_TO_TU(beacon_xmit_ind->duration);
				beacon_report->phyType = bss_desc->nwType;
				beacon_report->bcnProbeRsp = 1;
				beacon_report->rsni = bss_desc->sinr;
				beacon_report->rcpi = bss_desc->rssi;
				beacon_report->antennaId = 0;
				beacon_report->parentTSF = bss_desc->parentTSF;
				qdf_mem_copy(beacon_report->bssid,
					bss_desc->bssId, sizeof(tSirMacAddr));
			}

			switch (curr_req->request.Beacon.reportingDetail) {
			case BEACON_REPORTING_DETAIL_NO_FF_IE:
				/* 0: No need to include any elements. */
				pe_debug("No reporting detail requested");
				break;
			case BEACON_REPORTING_DETAIL_ALL_FF_REQ_IE:
				/* 1: Include all FFs and Requested Ies. */
				pe_debug("Only requested IEs in reporting detail requested");

				if (!bss_desc)
					break;

				rem_len = rrm_fill_beacon_ies(mac_ctx,
					    (uint8_t *)&beacon_report->Ies[0],
					    (uint8_t *)&beacon_report->numIes,
					    BEACON_REPORT_MAX_IES,
					    curr_req->request.Beacon.reqIes.
					    pElementIds,
					    curr_req->request.Beacon.reqIes.num,
					    offset, bss_desc);
				break;
			case BEACON_REPORTING_DETAIL_ALL_FF_IE:
				/* 2: default - Include all FFs and all Ies. */
			default:
				pe_debug("Default all IEs and FFs");
				if (!bss_desc)
					break;

				rem_len = rrm_fill_beacon_ies(mac_ctx,
					    (uint8_t *) &beacon_report->Ies[0],
					    (uint8_t *) &beacon_report->numIes,
					    BEACON_REPORT_MAX_IES,
					    NULL,
					    0,
					    offset, bss_desc);
				break;
			}
			beacon_report->frame_body_frag_id.id = bss_desc_count;
			beacon_report->frame_body_frag_id.frag_id = frag_id;
			/*
			 * If remaining length is non-zero, the beacon needs to
			 * be fragmented only if the current request supports
			 * last beacon report indication.
			 * If last beacon report indication is not supported,
			 * truncate and move on to the next beacon.
			 */
			if (rem_len &&
			    curr_req->request.Beacon.
			    last_beacon_report_indication) {
				offset = GET_IE_LEN_IN_BSS(
						bss_desc->length) - rem_len;
				pe_debug("offset %d ie_len %lu rem_len %d frag_id %d",
					 offset,
					 GET_IE_LEN_IN_BSS(bss_desc->length),
					 rem_len, frag_id);
				frag_id++;
				beacon_report->frame_body_frag_id.more_frags =
									true;
			} else {
				offset = 0;
				beacon_report->frame_body_frag_id.more_frags =
									false;
				frag_id = 0;
				bss_desc_count++;
				pe_debug("No remaining IEs");
			}

			if (curr_req->request.Beacon.
			    last_beacon_report_indication) {
				pe_debug("Setting last beacon report support");
				beacon_report->last_bcn_report_ind_support = 1;
			}
		}

		pe_debug("Total reports filled %d", i);
		num_frames = i / RADIO_REPORTS_MAX_IN_A_FRAME;
		if (i % RADIO_REPORTS_MAX_IN_A_FRAME)
			num_frames++;

		for (j = 0; j < num_frames; j++) {
			num_reports_in_frame = QDF_MIN((i - report_index),
						RADIO_REPORTS_MAX_IN_A_FRAME);

			pe_debug("Sending Action frame number %d",
				 num_reports_in_frame);
			lim_send_radio_measure_report_action_frame(mac_ctx,
				curr_req->dialog_token, num_reports_in_frame,
				(j == num_frames - 1) ? true : false,
				&report[report_index],
				beacon_xmit_ind->bssId, session_entry);
			report_index += num_reports_in_frame;
		}
		curr_req->sendEmptyBcnRpt = false;
	}

end:
	for (counter = 0; counter < beacon_xmit_ind->numBssDesc; counter++)
		qdf_mem_free(beacon_xmit_ind->pBssDescription[counter]);

	if (beacon_xmit_ind->fMeasureDone) {
		pe_debug("Measurement done....cleanup the context");
		rrm_cleanup(mac_ctx);
	}

	if (NULL != report)
		qdf_mem_free(report);

	return status;
}

static void rrm_process_beacon_request_failure(tpAniSirGlobal pMac,
					       tpPESession pSessionEntry,
					       tSirMacAddr peer,
					       tRrmRetStatus status)
{
	tpSirMacRadioMeasureReport pReport = NULL;
	tpRRMReq pCurrentReq = pMac->rrm.rrmPEContext.pCurrentReq;

	pReport = qdf_mem_malloc(sizeof(tSirMacRadioMeasureReport));
	if (NULL == pReport) {
		pe_err("Unable to allocate memory during RRM Req processing");
		return;
	}
	pReport->token = pCurrentReq->token;
	pReport->type = SIR_MAC_RRM_BEACON_TYPE;

	pe_debug("status %d token %d", status, pReport->token);

	switch (status) {
	case eRRM_REFUSED:
		pReport->refused = 1;
		break;
	case eRRM_INCAPABLE:
		pReport->incapable = 1;
		break;
	default:
		pe_err("Beacon request processing failed no report sent with status %d",
			       status);
		qdf_mem_free(pReport);
		return;
	}

	lim_send_radio_measure_report_action_frame(pMac,
						   pCurrentReq->dialog_token,
						   1, true,
						   pReport, peer,
						   pSessionEntry);

	qdf_mem_free(pReport);
	return;
}

/**
 * rrm_process_beacon_req() - Update curr_req and report
 * @mac_ctx: Global pointer to MAC context
 * @peer: Macaddress of the peer requesting the radio measurement
 * @session_entry: session entry
 * @curr_req: Pointer to RRM request
 * @radiomes_report: Pointer to radio measurement report
 * @rrm_req: Array of Measurement request IEs
 * @num_report: No.of reports
 * @index: Index for Measurement request
 *
 * Update structure sRRMReq and sSirMacRadioMeasureReport and pass it to
 * rrm_process_beacon_report_req().
 *
 * Return: tSirRetStatus
 */
static
tSirRetStatus rrm_process_beacon_req(tpAniSirGlobal mac_ctx, tSirMacAddr peer,
			     tpPESession session_entry, tpRRMReq curr_req,
			     tpSirMacRadioMeasureReport *radiomes_report,
			     tDot11fRadioMeasurementRequest *rrm_req,
			     uint8_t *num_report, int index)
{
	tRrmRetStatus rrm_status = eRRM_SUCCESS;
	tpSirMacRadioMeasureReport report;

	if (curr_req) {
		if (*radiomes_report == NULL) {
			/*
			 * Allocate memory to send reports for
			 * any subsequent requests.
			 */
			*radiomes_report = qdf_mem_malloc(sizeof(*report) *
				(rrm_req->num_MeasurementRequest - index));
			if (NULL == *radiomes_report) {
				pe_err("Unable to allocate memory during RRM Req processing");
				return eSIR_MEM_ALLOC_FAILED;
			}
			pe_debug("rrm beacon type refused of %d report in beacon table",
				*num_report);
		}
		report = *radiomes_report;
		report[*num_report].refused = 1;
		report[*num_report].type = SIR_MAC_RRM_BEACON_TYPE;
		report[*num_report].token =
			rrm_req->MeasurementRequest[index].measurement_token;
		(*num_report)++;
		return eSIR_SUCCESS;
	} else {
		curr_req = qdf_mem_malloc(sizeof(*curr_req));
		if (NULL == curr_req) {
			pe_err("Unable to allocate memory during RRM Req processing");
				qdf_mem_free(*radiomes_report);
			return eSIR_MEM_ALLOC_FAILED;
		}
		pe_debug("Processing Beacon Report request");
		curr_req->dialog_token = rrm_req->DialogToken.token;
		curr_req->token = rrm_req->
				  MeasurementRequest[index].measurement_token;
		curr_req->sendEmptyBcnRpt = true;
		mac_ctx->rrm.rrmPEContext.pCurrentReq = curr_req;
		rrm_status = rrm_process_beacon_report_req(mac_ctx, curr_req,
			&rrm_req->MeasurementRequest[index], session_entry);
		if (eRRM_SUCCESS != rrm_status) {
			rrm_process_beacon_request_failure(mac_ctx,
				session_entry, peer, rrm_status);
			rrm_cleanup(mac_ctx);
		}
	}
	return eSIR_SUCCESS;
}

/**
 * update_rrm_report() - Set incapable bit
 * @mac_ctx: Global pointer to MAC context
 * @report: Pointer to radio measurement report
 * @rrm_req: Array of Measurement request IEs
 * @num_report: No.of reports
 * @index: Index for Measurement request
 *
 * Send a report with incapabale bit set
 *
 * Return: tSirRetStatus
 */
static
tSirRetStatus update_rrm_report(tpAniSirGlobal mac_ctx,
				tpSirMacRadioMeasureReport *report,
				tDot11fRadioMeasurementRequest *rrm_req,
				uint8_t *num_report, int index)
{
	tpSirMacRadioMeasureReport rrm_report;

	if (!*report) {
		/*
		 * Allocate memory to send reports for
		 * any subsequent requests.
		 */
		*report = qdf_mem_malloc(sizeof(tSirMacRadioMeasureReport) *
			 (rrm_req->num_MeasurementRequest - index));
		if (!*report) {
			pe_err("Fail to alloc mem during RRM Req processing");
			return eSIR_MEM_ALLOC_FAILED;
		}
		pe_debug("rrm beacon type incapable of %d report", *num_report);
	}
	rrm_report = *report;
	rrm_report[*num_report].incapable = 1;
	rrm_report[*num_report].type =
		rrm_req->MeasurementRequest[index].measurement_type;
	rrm_report[*num_report].token =
		 rrm_req->MeasurementRequest[index].measurement_token;
	(*num_report)++;
	return eSIR_SUCCESS;
}

/* -------------------------------------------------------------------- */
/**
 * rrm_process_radio_measurement_request - Process rrm request
 * @mac_ctx: Global pointer to MAC context
 * @peer: Macaddress of the peer requesting the radio measurement.
 * @rrm_req: Array of Measurement request IEs
 * @session_entry: session entry.
 *
 * Processes the Radio Resource Measurement request.
 *
 * Return: tSirRetStatus
 */
tSirRetStatus
rrm_process_radio_measurement_request(tpAniSirGlobal mac_ctx,
				      tSirMacAddr peer,
				      tDot11fRadioMeasurementRequest *rrm_req,
				      tpPESession session_entry)
{
	uint8_t i;
	tSirRetStatus status = eSIR_SUCCESS;
	tpSirMacRadioMeasureReport report = NULL;
	uint8_t num_report = 0;
	tpRRMReq curr_req = mac_ctx->rrm.rrmPEContext.pCurrentReq;

	if (!rrm_req->num_MeasurementRequest) {
		report = qdf_mem_malloc(sizeof(tSirMacRadioMeasureReport));
		if (NULL == report) {
			pe_err("Unable to allocate memory during RRM Req processing");
			return eSIR_MEM_ALLOC_FAILED;
		}
		pe_err("No requestIes in the measurement request, sending incapable report");
		report->incapable = 1;
		num_report = 1;
		lim_send_radio_measure_report_action_frame(mac_ctx,
			rrm_req->DialogToken.token, num_report, true,
			report, peer, session_entry);
		qdf_mem_free(report);
		return eSIR_FAILURE;
	}
	/* PF Fix */
	if (rrm_req->NumOfRepetitions.repetitions > 0) {
		pe_debug("number of repetitions %d",
			rrm_req->NumOfRepetitions.repetitions);
		/*
		 * Send a report with incapable bit set.
		 * Not supporting repetitions.
		 */
		report = qdf_mem_malloc(sizeof(tSirMacRadioMeasureReport));
		if (NULL == report) {
			pe_err("Unable to allocate memory during RRM Req processing");
			return eSIR_MEM_ALLOC_FAILED;
		}
		report->incapable = 1;
		report->type = rrm_req->MeasurementRequest[0].measurement_type;
		num_report = 1;
		goto end;
	}

	for (i = 0; i < rrm_req->num_MeasurementRequest; i++) {
		switch (rrm_req->MeasurementRequest[i].measurement_type) {
		case SIR_MAC_RRM_BEACON_TYPE:
			/* Process beacon request. */
			status = rrm_process_beacon_req(mac_ctx, peer,
				 session_entry, curr_req, &report, rrm_req,
				 &num_report, i);
			if (eSIR_SUCCESS != status)
				return status;
			break;
		case SIR_MAC_RRM_LCI_TYPE:
		case SIR_MAC_RRM_LOCATION_CIVIC_TYPE:
		case SIR_MAC_RRM_FINE_TIME_MEAS_TYPE:
			pe_debug("RRM with type: %d sent to userspace",
			    rrm_req->MeasurementRequest[i].measurement_type);
			break;
		default:
			/* Send a report with incapabale bit set. */
			status = update_rrm_report(mac_ctx, &report, rrm_req,
						   &num_report, i);
			if (eSIR_SUCCESS != status)
				return status;
			break;
		}
	}

end:
	if (report) {
		lim_send_radio_measure_report_action_frame(mac_ctx,
			rrm_req->DialogToken.token, num_report, true,
			report, peer, session_entry);
		qdf_mem_free(report);
	}
	return status;
}

/* -------------------------------------------------------------------- */
/**
 * rrm_update_start_tsf
 **
 * FUNCTION:  Store start TSF of measurement.
 *
 * LOGIC:
 *
 * ASSUMPTIONS:
 *
 * NOTE:
 *
 * @param startTSF - TSF value at the start of measurement.
 * @return None
 */
void rrm_update_start_tsf(tpAniSirGlobal pMac, uint32_t startTSF[2])
{
	pMac->rrm.rrmPEContext.startTSF[0] = startTSF[0];
	pMac->rrm.rrmPEContext.startTSF[1] = startTSF[1];
}

/* -------------------------------------------------------------------- */
/**
 * rrm_get_start_tsf
 *
 * FUNCTION:  Get the Start TSF.
 *
 * LOGIC:
 *
 * ASSUMPTIONS:
 *
 * NOTE:
 *
 * @param startTSF - store star TSF in this buffer.
 * @return txPower
 */
void rrm_get_start_tsf(tpAniSirGlobal pMac, uint32_t *pStartTSF)
{
	pStartTSF[0] = pMac->rrm.rrmPEContext.startTSF[0];
	pStartTSF[1] = pMac->rrm.rrmPEContext.startTSF[1];

}

/* -------------------------------------------------------------------- */
/**
 * rrm_get_capabilities
 *
 * FUNCTION:
 * Returns a pointer to tpRRMCaps with all the caps enabled in RRM
 *
 * LOGIC:
 *
 * ASSUMPTIONS:
 *
 * NOTE:
 *
 * @param pSessionEntry
 * @return pointer to tRRMCaps
 */
tpRRMCaps rrm_get_capabilities(tpAniSirGlobal pMac, tpPESession pSessionEntry)
{
	return &pMac->rrm.rrmPEContext.rrmEnabledCaps;
}

/* -------------------------------------------------------------------- */
/**
 * rrm_initialize
 *
 * FUNCTION:
 * Initialize RRM module
 *
 * LOGIC:
 *
 * ASSUMPTIONS:
 *
 * NOTE:
 *
 * @return None
 */

tSirRetStatus rrm_initialize(tpAniSirGlobal pMac)
{
	tpRRMCaps pRRMCaps = &pMac->rrm.rrmPEContext.rrmEnabledCaps;

	pMac->rrm.rrmPEContext.pCurrentReq = NULL;
	pMac->rrm.rrmPEContext.txMgmtPower = 0;
	pMac->rrm.rrmPEContext.DialogToken = 0;

	pMac->rrm.rrmPEContext.rrmEnable = 0;
	pMac->rrm.rrmPEContext.prev_rrm_report_seq_num = 0xFFFF;

	qdf_mem_set(pRRMCaps, sizeof(tRRMCaps), 0);
	pRRMCaps->LinkMeasurement = 1;
	pRRMCaps->NeighborRpt = 1;
	pRRMCaps->BeaconPassive = 1;
	pRRMCaps->BeaconActive = 1;
	pRRMCaps->BeaconTable = 1;
	pRRMCaps->APChanReport = 1;
	pRRMCaps->fine_time_meas_rpt = 1;
	pRRMCaps->lci_capability = 1;

	pRRMCaps->operatingChanMax = 3;
	pRRMCaps->nonOperatingChanMax = 3;

	return eSIR_SUCCESS;
}

/* -------------------------------------------------------------------- */
/**
 * rrm_cleanup
 *
 * FUNCTION:
 * cleanup RRM module
 *
 * LOGIC:
 *
 * ASSUMPTIONS:
 *
 * NOTE:
 *
 * @param mode
 * @param rate
 * @return None
 */

tSirRetStatus rrm_cleanup(tpAniSirGlobal pMac)
{
	if (pMac->rrm.rrmPEContext.pCurrentReq) {
		if (pMac->rrm.rrmPEContext.pCurrentReq->request.Beacon.reqIes.
		    pElementIds) {
			qdf_mem_free(pMac->rrm.rrmPEContext.pCurrentReq->
				     request.Beacon.reqIes.pElementIds);
		}

		qdf_mem_free(pMac->rrm.rrmPEContext.pCurrentReq);
	}

	pMac->rrm.rrmPEContext.pCurrentReq = NULL;
	return eSIR_SUCCESS;
}

/**
 * lim_update_rrm_capability() - Update PE context's rrm capability
 * @mac_ctx: Global pointer to MAC context
 * @join_req: Pointer to SME join request.
 *
 * Update PE context's rrm capability based on SME join request.
 *
 * Return: None
 */
void lim_update_rrm_capability(tpAniSirGlobal mac_ctx,
			       tpSirSmeJoinReq join_req)
{
	mac_ctx->rrm.rrmPEContext.rrmEnable = join_req->rrm_config.rrm_enabled;
	qdf_mem_copy(&mac_ctx->rrm.rrmPEContext.rrmEnabledCaps,
		     &join_req->rrm_config.rm_capability,
		     RMENABLEDCAP_MAX_LEN);

	return;
}
