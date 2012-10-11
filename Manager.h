/*
 * Manager.h
 *
 *  Created on: 26 Sep 2012
 *      Author: jack
 */

#ifndef MANAGER_H_
#define MANAGER_H_

#include <Arduino.h>
#include "consts.h"
#include "Rfm12b.h"
#include "Packet.h"
#include "Sensor.h"

class Manager {
public:
	Manager();
	void init();
	void run();
private:
    Rfm12b rfm;

	/*****************************************
	 * CC TX (e.g. whole-house transmitters) *
	 *****************************************/
	static const uint8_t MAX_CC_TXS = 5;

	/* length of time we're willing to wait
	 * for a CC TX.  We'll open the window
	 * half of WINDOW before CC TX's ETA. */
	static const uint16_t CC_TX_WINDOW = 1000;

    Sensor cc_txs[MAX_CC_TXS];
    Sensor* p_next_cc_tx; // The next expected CC TX
    uint8_t num_cc_txs;

    /*****************************************
     * CC TRX (e.g. EDF IAMs)                *
     *****************************************/
    static const uint8_t  MAX_CC_TRXS = 35;   /* number of CC TRXs (e.g. EDF IAMs) */
	static const uint32_t CC_TRX_TIMEOUT = 100; // milliseconds to wait for reply
    uint32_t cc_trx_ids[MAX_CC_TRXS];
    uint8_t  i_next_cc_trx; // index into cc_trx_ids
	uint32_t id_next_cc_trx;// actual id (i.e. a copy of a cc_trx_ids[i_next_cc_trx])
	uint8_t num_cc_trxs;

	uint8_t retries; // for polling CC TRXs
	static const uint8_t MAX_RETRIES = 5; // for polling CC TRXs

	uint32_t timecode_polled_first_cc_trx;

	/***************************
	 * Private methods
	 ***************************/

	/* Poll CC TRX (e.g. EDF IAM) with ID == id_next_cc_trx
	 * Listen for response. */
	void poll_next_cc_trx();

	void wait_for_cc_tx();

	void find_next_expected_cc_tx();

	/* Loop through the cc_txs array and return a pointer
	 * to the Sensor with the appropriate id.
	 * If no Sensor is found then returns NULL.
	 */
	Sensor* find_cc_tx(const uint32_t& id);

	const bool id_is_cc_trx(const uint32_t& id) const;

	void increment_i_of_next_cc_trx();

	/**
	 * Process every packet in rx_packet_buffer appropriately
	 *
	 * @return true if a packet corresponding to id is found
	 */
	const bool process_rx_pack_buf_and_find_id(const uint32_t& id);
};

#endif /* MANAGER_H_ */