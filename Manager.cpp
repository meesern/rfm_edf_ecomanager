/*
 * Manager.cpp
 *
 *  Created on: 26 Sep 2012
 *      Author: jack
 */

#include "Manager.h"
#include "Logger.h"
#include "utils.h"

Manager::Manager()
: auto_pair(true), pair_with(ID_INVALID), print_packets(ALL_VALID),
  retries(0), time_to_start_next_trx_roll_call(0)  {}


void Manager::init()
{
    // todo check that this works in the Manager() constructor, then
    //      remove init()
    rfm.init();
    rfm.enable_rx();
    time_to_start_next_trx_roll_call = millis();
}


void Manager::run()
{
    using namespace utils;
    //************* HANDLE TRANSMITTERS AND TRANSCEIVERS ***********
    if (cc_txs.get_n() == 0) {
        // There are no CC TXs so all we have to do is poll TRXs
        poll_next_cc_trx();
    } else {
        if (in_future(cc_txs.current().get_eta() - CC_TX_WINDOW_OPEN)) {
            // We're far enough away from the next expected CC TX transmission
            // to mean that we have time to poll TRXs
            poll_next_cc_trx();
        } else {
            wait_for_cc_tx();
        }
    }

    // Make sure we always check for RX packets
    process_rx_pack_buf_and_find_id(0);

    if (Serial.available()) {
        handle_serial_commands();
    }
}


void Manager::handle_serial_commands()
{
    char incomming_byte = Serial.read();
    uint32_t input;

    switch (incomming_byte) {
    case 'a': auto_pair = true;  Serial.println(F("ACK auto_pair on")); break;
    case 'm': auto_pair = false; Serial.println(F("ACK auto_pair off")); break;
    case 'p':
        if (auto_pair) {
            Serial.println(F("NAK Enable manual pairing before 'p' cmd."));
        } else {
            Serial.println(F("ACK Enter ID:"));
            input = utils::read_uint32_from_serial();
            if (input == UINT32_INVALID) {
                Serial.println(F("NAK"));
            } else {
                pair_with = input;
                Serial.print(F("ACK pair_with set to "));
                Serial.println(pair_with);
            }
        }
        break;
    case 'v':
#ifdef LOGGING
        Serial.print(F("ACK enter log level: "));
        print_log_levels();
        input = utils::read_uint32_from_serial();
        if (input == UINT32_INVALID) {
            Serial.println(F("NAK"));
        } else {
            Logger::log_threshold = (Level)input;
            Serial.print(F("ACK Log level set to "));
            print_log_level(Logger::log_threshold);
            Serial.println(F(""));
        }
#else
        Serial.println(F("NAK logging disabled!"));
#endif // LOGGING
        break;
    case 'k': print_packets = ONLY_KNOWN; Serial.println(F("ACK only print data from known transmitters")); break;
    case 'u': print_packets = ALL_VALID; Serial.println(F("ACK print all valid packets")); break;
    case 'b': print_packets = ALL; Serial.println(F("ACK print all")); break;
    case 'n': cc_txs.get_id_from_serial();  break;
    case 'N': cc_trxs.get_id_from_serial(); break;
    case 's': cc_txs.set_size_from_serial(); break;
    case 'S': cc_trxs.set_size_from_serial(); break;
    case 'd': cc_txs.delete_all();  break;
    case 'D': cc_trxs.delete_all(); break;
    case 'r': cc_txs.remove_id_from_serial();  break;
    case 'R': cc_trxs.remove_id_from_serial();  break;
    case 'l': cc_txs.print();  break;
    case 'L': cc_trxs.print(); break;
    case '0': change_state(0); break;
    case '1': change_state(1); break;
    case 't': delay(10); Serial.println(millis()); break;
    case '\r': break; // ignore carriage returns
    default:
        Serial.print(F("NAK unrecognised command '"));
        Serial.print(incomming_byte);
        Serial.println(F("'"));
        break;
    }
}


void Manager::poll_next_cc_trx()
{
    if (cc_trxs.get_n() == 0) return;
    using namespace utils;

	// don't continually poll TRXs;
    // instead only do one roll call per SAMPLE_PERIOD
	if (cc_trxs.get_i()==0 && retries==0) {
		if (in_future(time_to_start_next_trx_roll_call)) {
			return; // We've finished polling all TRXs for this SAMPLE_PERIOD.
		} else {
			time_to_start_next_trx_roll_call = millis() + SAMPLE_PERIOD;
		}
	}

	rfm.poll_cc_trx(cc_trxs.current().id);
	const bool success = wait_for_response(cc_trxs.current().id, CC_TRX_TIMEOUT);

	if (success) {
        // We got a reply from the TRX we polled
	    cc_trxs.current().active = true;
		cc_trxs.next();
		retries = 0;
	} else {
	    // We didn't get a reply from the TRX we polled
		if (retries < MAX_RETRIES && cc_trxs.current().active) {
            log(DEBUG, PSTR("Missing TRX %lu, retries=%d"), cc_trxs.current().id, retries);
			retries++;
		} else {
		    cc_trxs.current().active = false;
			log(INFO, PSTR("Missing TRX %lu. Giving up."), cc_trxs.current().id);
            cc_trxs.next();
			retries = 0;
		}
	}
}


void Manager::wait_for_cc_tx()
{
    // listen for TX for defined period.
    log(DEBUG, PSTR("Window open! Expecting %lu at %lu"), cc_txs.current().id, cc_txs.current().get_eta());
    bool success = wait_for_response(cc_txs.current().id, CC_TX_WINDOW);
    log(DEBUG, PSTR("Window closed. success=%d"), success);

    if (!success) {
        // tell whole-house TX it missed its slot
        cc_txs.current().missing();
        cc_txs.next();
    }
}


bool Manager::wait_for_response(const id_t& id, const millis_t& wait_duration)
{
    using namespace utils;

    const millis_t end_time = millis() + wait_duration;
    bool success = false;

    log(DEBUG, PSTR("Waiting %lu ms for ID %lu"), wait_duration, id);
    while (in_future(end_time)) {
        if (process_rx_pack_buf_and_find_id(id)) {
            // We got a reply from the TRX we polled
            success = true;
            break;
        }
    }
    return success;
}


bool Manager::process_rx_pack_buf_and_find_id(const id_t& target_id)
{
    bool success = false;
    TxType tx_type;
	id_t id;
	RXPacket* packet = NULL; // just using this pointer to make code more readable

	/* Loop through every packet in packet buffer. If it's done then post-process it
	 * and then check if it's valid.  If so then handle the different types of
	 * packet.  Finally reset the packet and return.
	 */
	for (index_t packet_i=0; packet_i<PACKET_BUF_LENGTH; packet_i++) {

		packet = &rfm.rx_packet_buffer.packets[packet_i];
		if (packet->done()) {
            tx_type = packet->get_tx_type();
			if (packet->is_ok()) {
	            id = packet->get_id();
                success |= (id == target_id); // Was this the packet we were looking for?

				//******** PAIRING REQUEST **********************
				if (packet->is_pairing_request()) {
				    packet->reset();
				    handle_pair_request(*packet);
				    break;
				}

				//********* CC TX (transmit-only sensor) ********
				switch (tx_type) {
				case TX:
				    bool found;
				    index_t cc_tx_i;
				    found = cc_txs.find(id, cc_tx_i);
				    if (found) { // received ID is a CC_TX id we know about
                        packet->print_id_and_watts(); // send data over serial
				        cc_txs[cc_tx_i].update(*packet);
				        cc_txs.next();
				    } else {
				        log(INFO, PSTR("Rx'd CC_TX packet with unknown ID %lu"), id);
				        if (print_packets >= ALL_VALID) {
				            packet->print_id_and_watts(); // send data over serial
				        }
				    }
				    break;
				case TRX:
				    //****** CC TRX (transceiver; e.g. EDF IAM) ******
				    if (cc_trxs.find(id)) {
				        // Received ID is a CC_TRX id we know about
				        packet->print_id_and_watts(); // send data over serial
				    }
				    //********* UNKNOWN TRX ID *************************
				    else {
				        log(INFO, PSTR("Rx'd CC_TRX packet with unknown ID %lu"), id);
				        if (print_packets >= ALL_VALID) {
				            packet->print_id_and_watts(); // send data over serial
				        }
				    }
				    break;
				}

			} else { // packet is not OK
				log(INFO, PSTR("Rx'd broken %s packet"), tx_type==TX ? "TX" : "TRX");
				if (print_packets == ALL) {
				    packet->print_bytes();
				}
			}
	        packet->reset();
		}
	}

	return success;
}


void Manager::handle_pair_request(const RXPacket& packet)
{
    const TxType tx_type = packet.get_tx_type();
    const id_t id = packet.get_id();

    log(DEBUG, PSTR("Pair req from %lu"), id);
    if (tx_type==TX && cc_txs.find(id)) {
        // ignore pair request from CC_TX we're already paired with
    } else if (tx_type==TRX && cc_trxs.find(id)) {
        // pair request from CC_TRX we previously attempted to pair with
        // this means our ACK response failed so try again
        pair_with = id;
        pair(packet);
    } else if (auto_pair) {
        // Auto pair mode. Go ahead and pair.
        pair_with = id;
        pair(packet);
    } else if (pair_with == id) {
        // Manual pair mode and pair_with has already been set so pair.
        pair(packet);
    } else {
        // Manual pair mode. Tell user about pair request.
        Serial.print(F("{\"pr\": "));
        packet.print_id_and_type(true);
        Serial.println(F("}"));
    }
}


void Manager::pair(const RXPacket& packet)
{
    bool success = false;

    switch (packet.get_tx_type()) {
    case TX:
        success = cc_txs.append(pair_with);
        break;
    case TRX: // transceiver. So we need to ACK.
        rfm.ack_cc_trx(pair_with);
        success = cc_trxs.append(pair_with);
        // Note that this may be a re-try to ACK a TRX we
        // previously added if our first ACK failed.
        break;
    }

    if (success) {
        Serial.print(F("{\"pw\": "));
        packet.print_id_and_type(true);
        Serial.println(F(" }"));
    }

    pair_with = ID_INVALID; // reset
}

void Manager::change_state(const bool state) const
{
#ifndef TESTING
    Serial.println(F("ACK enter id of TRX to switch:"));

    id_t id_to_switch = utils::read_uint32_from_serial();
    if (id_to_switch == UINT32_INVALID) {
        Serial.println(F("NAK"));
        return;
    }

    if (state) { // Turn on
        rfm.send_command_to_trx('O', 'N', id_to_switch);
    } else { // Turn off
        rfm.send_command_to_trx('O', 'F', id_to_switch);
    }

    // TODO: check response from TRX

    Serial.println(F("ACK"));
#endif // TESTING
}
