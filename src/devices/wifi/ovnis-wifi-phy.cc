/* -*-  Mode: C++; c-file-style: "gnu"; indent-tabs-mode:nil; -*- */
/*
 * Copyright (c) 2005,2006 INRIA
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as 
 * published by the Free Software Foundation;
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
 *
 * Author: Mathieu Lacage <mathieu.lacage@sophia.inria.fr>
 */

#include "ovnis-wifi-phy.h"
#include "ovnis-wifi-channel.h"
#include "ns3/wifi-mode.h"
#include "ns3/wifi-preamble.h"
//#include "devices/wifi/wifi-phy-state-helper.h"
#include "wifi/model/wifi-phy-state-helper.h"
#include "ns3/error-rate-model.h"
#include "ns3/simulator.h"
#include "ns3/packet.h"
//#include "ns3/random-variable.h"
#include "ns3/random-variable-stream.h"
#include "ns3/assert.h"
#include "ns3/log.h"
#include "ns3/double.h"
#include "ns3/uinteger.h"
#include "ns3/enum.h"
#include "ns3/pointer.h"
#include "ns3/net-device.h"
#include "ns3/trace-source-accessor.h"
#include <math.h>
#include "common/myEnergy-tag.h"
#include "common/myTxEnergy-tag.h"
#include "ns3/wifi-mac-header.h"
#include "log.h"

NS_LOG_COMPONENT_DEFINE ("OvnisWifiPhy");

namespace ns3 {

NS_OBJECT_ENSURE_REGISTERED (OvnisWifiPhy);

TypeId 
OvnisWifiPhy::GetTypeId (void)
{
  static TypeId tid = TypeId ("ns3::OvnisWifiPhy")
    .SetParent<WifiPhy> ()
    .AddConstructor<OvnisWifiPhy> ()
    .AddAttribute ("EnergyDetectionThreshold",
                   "The energy of a received signal should be higher than "
                   "this threshold (dbm) to allow the PHY layer to detect the signal.",
                   DoubleValue (-96.0),
                   MakeDoubleAccessor (&OvnisWifiPhy::SetEdThreshold,
                                       &OvnisWifiPhy::GetEdThreshold),
                   MakeDoubleChecker<double> ())
    .AddAttribute ("CcaMode1Threshold",
                   "The energy of a received signal should be higher than "
                   "this threshold (dbm) to allow the PHY layer to declare CCA BUSY state",
                   DoubleValue (-99.0),
                   MakeDoubleAccessor (&OvnisWifiPhy::SetCcaMode1Threshold,
                                       &OvnisWifiPhy::GetCcaMode1Threshold),
                   MakeDoubleChecker<double> ())
    .AddAttribute ("TxGain",
                   "Transmission gain (dB).",
                   DoubleValue (1.0),
                   MakeDoubleAccessor (&OvnisWifiPhy::SetTxGain,
                                       &OvnisWifiPhy::GetTxGain),
                   MakeDoubleChecker<double> ())
    .AddAttribute ("RxGain",
                   "Reception gain (dB).",
                   DoubleValue (1.0),
                   MakeDoubleAccessor (&OvnisWifiPhy::SetRxGain,
                                       &OvnisWifiPhy::GetRxGain),
                   MakeDoubleChecker<double> ())
    .AddAttribute ("TxPowerLevels",
                   "Number of transmission power levels available between "
                   "TxPowerBase and TxPowerEnd included.",
                   UintegerValue (1),
                   MakeUintegerAccessor (&OvnisWifiPhy::m_nTxPower),
                   MakeUintegerChecker<uint32_t> ())
    .AddAttribute ("TxPowerEnd",
                   "Maximum available transmission level (dbm).",
                   DoubleValue (16.0206),
                   MakeDoubleAccessor (&OvnisWifiPhy::SetTxPowerEnd,
                                       &OvnisWifiPhy::GetTxPowerEnd),
                   MakeDoubleChecker<double> ())
    .AddAttribute ("TxPowerStart",
                   "Minimum available transmission level (dbm).",
                   DoubleValue (16.0206),
                   MakeDoubleAccessor (&OvnisWifiPhy::SetTxPowerStart,
                                       &OvnisWifiPhy::GetTxPowerStart),
                   MakeDoubleChecker<double> ())
    .AddAttribute ("RxNoiseFigure",
                   "Loss (dB) in the Signal-to-Noise-Ratio due to non-idealities in the receiver."
                   " According to Wikipedia (http://en.wikipedia.org/wiki/Noise_figure), this is "
                   "\"the difference in decibels (dB) between"
                   " the noise output of the actual receiver to the noise output of an "
                   " ideal receiver with the same overall gain and bandwidth when the receivers "
                   " are connected to sources at the standard noise temperature T0 (usually 290 K)\"."
                   " For",
                   DoubleValue (7),
                   MakeDoubleAccessor (&OvnisWifiPhy::SetRxNoiseFigure,
                                       &OvnisWifiPhy::GetRxNoiseFigure),
                   MakeDoubleChecker<double> ())
    .AddAttribute ("State", "The state of the PHY layer",
                   PointerValue (),
                   MakePointerAccessor (&OvnisWifiPhy::m_state),
                   MakePointerChecker<WifiPhyStateHelper> ())
    .AddAttribute ("ChannelSwitchDelay",
                   "Delay between two short frames transmitted on different frequencies. NOTE: Unused now.",
                   TimeValue (MicroSeconds (250)),
                   MakeTimeAccessor (&OvnisWifiPhy::m_channelSwitchDelay),
                   MakeTimeChecker ())
    .AddAttribute ("ChannelNumber",
                   "Channel center frequency = Channel starting frequency + 5 MHz * (nch - 1)",
                   UintegerValue (1),
                   MakeUintegerAccessor (&OvnisWifiPhy::SetChannelNumber,
                                         &OvnisWifiPhy::GetChannelNumber),
                   MakeUintegerChecker<uint16_t> ())

    ;
  return tid;
}

OvnisWifiPhy::OvnisWifiPhy ()
  :  m_channelNumber (1),
     m_endRxEvent (),
	m_channelStartingFrequency (0)
{
  NS_LOG_FUNCTION (this);
  m_random = CreateObject<UniformRandomVariable> ();
  m_state = CreateObject<WifiPhyStateHelper> ();
}

OvnisWifiPhy::~OvnisWifiPhy ()
{
  NS_LOG_FUNCTION (this);
}

void
OvnisWifiPhy::DoDispose (void)
{
  NS_LOG_FUNCTION (this);
  m_channel = 0;
  m_deviceRateSet.clear ();
  m_device = 0;
  m_mobility = 0;
  m_state = 0;
}

void
OvnisWifiPhy::ConfigureStandard (enum WifiPhyStandard standard)
{
  NS_LOG_FUNCTION (this << standard);

  switch (standard)
      {
      case WIFI_PHY_STANDARD_80211a:
        Configure80211a ();
        break;
      case WIFI_PHY_STANDARD_80211b:
        Configure80211b ();
        break;
      case WIFI_PHY_STANDARD_80211g:
        Configure80211g ();
        break;
      case WIFI_PHY_STANDARD_80211_10MHZ:
        Configure80211_10Mhz ();
        break;
      case WIFI_PHY_STANDARD_80211_5MHZ:
        Configure80211_5Mhz ();
        break;
      case WIFI_PHY_STANDARD_holland:
        ConfigureHolland ();
        break;
      case WIFI_PHY_STANDARD_80211p_CCH:
        Configure80211p_CCH ();
        break;
      case WIFI_PHY_STANDARD_80211p_SCH:
        Configure80211p_SCH ();
        break;
      default:
        NS_ASSERT (false);
        break;
      }
}


void 
OvnisWifiPhy::SetRxNoiseFigure (double noiseFigureDb)
{
  NS_LOG_FUNCTION (this << noiseFigureDb);
  m_interference.SetNoiseFigure (DbToRatio (noiseFigureDb));
}
void 
OvnisWifiPhy::SetTxPowerStart (double start)
{
  NS_LOG_FUNCTION (this << start);
  m_txPowerBaseDbm = start;
}
void 
OvnisWifiPhy::SetTxPowerEnd (double end)
{
  NS_LOG_FUNCTION (this << end);
  m_txPowerEndDbm = end;
}
void 
OvnisWifiPhy::SetNTxPower (uint32_t n)
{
  NS_LOG_FUNCTION (this << n);
  m_nTxPower = n;
}
void 
OvnisWifiPhy::SetTxGain (double gain)
{
  NS_LOG_FUNCTION (this << gain);
  m_txGainDb = gain;
}
void 
OvnisWifiPhy::SetRxGain (double gain)
{
  NS_LOG_FUNCTION (this << gain);
  m_rxGainDb = gain;
}
void 
OvnisWifiPhy::SetEdThreshold (double threshold)
{
  NS_LOG_FUNCTION (this << threshold);
  m_edThresholdW = DbmToW (threshold);
}
void 
OvnisWifiPhy::SetCcaMode1Threshold (double threshold)
{
  NS_LOG_FUNCTION (this << threshold);
  m_ccaMode1ThresholdW = DbmToW (threshold);
}
void 
OvnisWifiPhy::SetErrorRateModel (Ptr<ErrorRateModel> rate)
{
  m_interference.SetErrorRateModel (rate);
}
void 
OvnisWifiPhy::SetDevice (Ptr<Object> device)
{
  m_device = device;
}
void 
OvnisWifiPhy::SetMobility (Ptr<Object> mobility)
{
  m_mobility = mobility;
}

double 
OvnisWifiPhy::GetRxNoiseFigure (void) const
{
  return RatioToDb (m_interference.GetNoiseFigure ());
}
double 
OvnisWifiPhy::GetTxPowerStart (void) const
{
  return m_txPowerBaseDbm;
}
double 
OvnisWifiPhy::GetTxPowerEnd (void) const
{
  return m_txPowerEndDbm;
}
double 
OvnisWifiPhy::GetTxGain (void) const
{
  return m_txGainDb;
}
double 
OvnisWifiPhy::GetRxGain (void) const
{
  return m_rxGainDb;
}

double 
OvnisWifiPhy::GetEdThreshold (void) const
{
  return WToDbm (m_edThresholdW);
}

double 
OvnisWifiPhy::GetCcaMode1Threshold (void) const
{
  return WToDbm (m_ccaMode1ThresholdW);
}

Ptr<ErrorRateModel> 
OvnisWifiPhy::GetErrorRateModel (void) const
{
  return m_interference.GetErrorRateModel ();
}
Ptr<Object> 
OvnisWifiPhy::GetDevice (void) const
{
  return m_device;
}
Ptr<Object> 
OvnisWifiPhy::GetMobility (void)
{
  return m_mobility;
}

double 
OvnisWifiPhy::CalculateSnr (WifiMode txMode, double ber) const
{
  return m_interference.GetErrorRateModel ()->CalculateSnr (txMode, ber);
}

Ptr<WifiChannel> 
OvnisWifiPhy::GetChannel (void) const
{
  return m_channel;
}
void 
OvnisWifiPhy::SetChannel (Ptr<OvnisWifiChannel> channel)
{
  m_channel = channel;
  m_channel->Add (this);
}

void 
OvnisWifiPhy::SetChannelNumber (uint16_t nch)
{
  if (Simulator::Now () == Seconds (0))
    {
      // this is not channel switch, this is initialization 
      NS_LOG_DEBUG("start at channel " << nch);
      m_channelNumber = nch;
      return;
    }

  NS_ASSERT (!IsStateSwitching()); 
  switch (m_state->GetState ()) {
  case OvnisWifiPhy::RX:
    NS_LOG_DEBUG ("drop packet because of channel switching while reception");
    m_endRxEvent.Cancel();
    goto switchChannel;
    break;
  case OvnisWifiPhy::TX:
      NS_LOG_DEBUG ("channel switching postponed until end of current transmission");
      Simulator::Schedule (GetDelayUntilIdle(), &OvnisWifiPhy::SetChannelNumber, this, nch);
    break;
  case OvnisWifiPhy::CCA_BUSY:
  case OvnisWifiPhy::IDLE:
    goto switchChannel;
    break;
  default:
    NS_ASSERT (false);
    break;
  }

  return;

  switchChannel: 

  NS_LOG_DEBUG("switching channel " << m_channelNumber << " -> " << nch);
  m_state->SwitchToChannelSwitching(m_channelSwitchDelay); 
  m_interference.EraseEvents(); 
  /*
   * Needed here to be able to correctly sensed the medium for the first
   * time after the switching. The actual switching is not performed until
   * after m_channelSwitchDelay. Packets received during the switching
   * state are added to the event list and are employed later to figure
   * out the state of the medium after the switching.
   */
  m_channelNumber = nch;
}

uint16_t 
OvnisWifiPhy::GetChannelNumber() const
{
  return m_channelNumber;
}

double
OvnisWifiPhy::GetChannelFrequencyMhz() const
{
  return m_channelStartingFrequency + 5 * GetChannelNumber();
}

void 
OvnisWifiPhy::SetReceiveOkCallback (RxOkCallback callback)
{
  m_state->SetReceiveOkCallback (callback);
}
void 
OvnisWifiPhy::SetReceiveErrorCallback (RxErrorCallback callback)
{
  m_state->SetReceiveErrorCallback (callback);
}
void 
OvnisWifiPhy::StartReceivePacket (Ptr<Packet> packet,
                                 double rxPowerDbm,
//                                 WifiMode txMode,
                                 WifiTxVector txVector,
                                 enum WifiPreamble preamble)
{
	 NS_LOG_FUNCTION (this << packet << rxPowerDbm << txVector.GetMode()<< preamble);
	 rxPowerDbm += m_rxGainDb;
	 double rxPowerW = DbmToW (rxPowerDbm);
	 Time rxDuration = CalculateTxDuration (packet->GetSize (), txVector, preamble);
	 WifiMode txMode=txVector.GetMode();
	 Time endRx = Simulator::Now () + rxDuration;

	Ptr<InterferenceHelper::Event> event;
	event = m_interference.Add (packet->GetSize (),
							  txMode,
							  preamble,
							  rxDuration,
							  rxPowerW,
				  txVector);  // we need it to calculate duration of HT training symbols

  switch (m_state->GetState ()) {
  case OvnisWifiPhy::SWITCHING:
    NS_LOG_DEBUG ("drop packet because of channel switching");
    NotifyRxDrop (packet);
    ovnis::Log::getInstance().packetDropped(OvnisWifiPhy::SWITCHING);
    /*
     * Packets received on the upcoming channel are added to the event list
     * during the switching state. This way the medium can be correctly sensed
     * when the device listens to the channel for the first time after the
     * switching e.g. after channel switching, the channel may be sensed as
     * busy due to other devices' tramissions started before the end of
     * the switching.
     */
    if (endRx > Simulator::Now () + m_state->GetDelayUntilIdle ()) 
      {
        // that packet will be noise _after_ the completion of the
        // channel switching.
        goto maybeCcaBusy;
      }
    break;
  case OvnisWifiPhy::RX:

    NS_LOG_DEBUG ("drop packet because already in Rx (power="<<
                  rxPowerW<<"W)");
    NotifyRxDrop (packet);
    ovnis::Log::getInstance().packetDropped(OvnisWifiPhy::RX);
    if (endRx > Simulator::Now () + m_state->GetDelayUntilIdle ()) 
      {
        // that packet will be noise _after_ the reception of the
        // currently-received packet.
        goto maybeCcaBusy;
      }
    break;
  case OvnisWifiPhy::TX:

    NS_LOG_DEBUG ("drop packet because already in Tx (power="<<
                  rxPowerW<<"W)");
    NotifyRxDrop (packet);
    ovnis::Log::getInstance().packetDropped(OvnisWifiPhy::TX);
    if (endRx > Simulator::Now () + m_state->GetDelayUntilIdle ()) 
      {
        // that packet will be noise _after_ the transmission of the
        // currently-transmitted packet.
        goto maybeCcaBusy;
      }
    break;
  case OvnisWifiPhy::CCA_BUSY:
  case OvnisWifiPhy::IDLE:

    if (rxPowerW > m_edThresholdW) 
      {

// Added by Patricia Ruiz to tell the high layers the rx power of the message
		  double rxPowerdBm = WToDbm(rxPowerW);

		  MyEnergyTag tagEg = MyEnergyTag();
		  tagEg.SetTagDouble(rxPowerdBm);
		  packet ->AddPacketTag (tagEg);
////
        NS_LOG_DEBUG ("sync to signal (power="<<rxPowerW<<"W)");
        // sync to signal
        m_state->SwitchToRx (rxDuration);
        NS_ASSERT (m_endRxEvent.IsExpired ());
        NotifyRxBegin (packet);
        m_interference.NotifyRxStart();
        m_endRxEvent = Simulator::Schedule (rxDuration, &OvnisWifiPhy::EndReceive, this,
                                            packet,
                                            event);
      }
    else 
      {
        NS_LOG_DEBUG ("drop packet because signal power too Small ("<<
                      rxPowerW<<"<"<<m_edThresholdW<<")");
        NotifyRxDrop (packet);
        goto maybeCcaBusy;
      }
    break;
  }

  return;

 maybeCcaBusy:
  // We are here because we have received the first bit of a packet and we are
  // not going to be able to synchronize on it
  // In this model, CCA becomes busy when the aggregation of all signals as
  // tracked by the InterferenceHelper class is higher than the CcaBusyThreshold

  Time delayUntilCcaEnd = m_interference.GetEnergyDuration (m_ccaMode1ThresholdW);
  if (!delayUntilCcaEnd.IsZero ())
    {
      m_state->SwitchMaybeToCcaBusy (delayUntilCcaEnd);
    }
}

void
OvnisWifiPhy::SendPacket (Ptr<const Packet> packet, WifiMode txMode, WifiPreamble preamble, WifiTxVector txVector)
{
  NS_LOG_FUNCTION (this << packet << txMode << preamble << (uint32_t)txVector.GetTxPowerLevel());
  /* Transmission can happen if:
   *  - we are syncing on a packet. It is the responsability of the
   *    MAC layer to avoid doing this but the PHY does nothing to
   *    prevent it.
   *  - we are idle
   */
  NS_ASSERT (!m_state->IsStateTx () && !m_state->IsStateSwitching ());

  Time txDuration = CalculateTxDuration (packet->GetSize (), txVector, preamble);
  if (m_state->IsStateRx ())
    {
      m_endRxEvent.Cancel ();
      m_interference.NotifyRxEnd ();
    }
  NotifyTxBegin (packet);
  uint32_t dataRate500KbpsUnits = txVector.GetMode().GetDataRate () * txVector.GetNss() / 500000;
  bool isShortPreamble = (WIFI_PREAMBLE_SHORT == preamble);
  NotifyMonitorSniffTx (packet, (uint16_t)GetChannelFrequencyMhz (), GetChannelNumber (), dataRate500KbpsUnits, isShortPreamble, txVector.GetTxPowerLevel());
  m_state->SwitchToTx (txDuration, packet, txVector.GetMode(), preamble,  txVector.GetTxPowerLevel());
  m_channel->Send (this, packet, GetPowerDbm ( txVector.GetTxPowerLevel()) + m_txGainDb, txVector, preamble);
}


//void
//OvnisWifiPhy::SendPacket (Ptr<const Packet> packet, WifiMode txMode, WifiPreamble preamble, uint8_t txPower)
//{
////  NotifyPromiscSniffTx (packet, (uint16_t)GetChannelFrequencyMhz (), GetChannelNumber (), dataRate500KbpsUnits, isShortPreamble);
//  NotifyMonitorSniffTx (packet, (uint16_t)GetChannelFrequencyMhz (), GetChannelNumber (), dataRate500KbpsUnits, isShortPreamble);
//  m_state->SwitchToTx (txDuration, packet, txMode, preamble, txPower);
//
//  //Added by Patricia Ruiz (for changing the tx power)
//  	MyTxEnergyTag neighborPower=MyTxEnergyTag();
//  	Ptr<Packet> packet2 = packet->Copy();
//  	bool aux =packet2->RemovePacketTag (neighborPower);
//  	if  (aux)	{
//  		//Get the estimated tx energy in dBm from the tag
//  		double rxPowerDbm = neighborPower.GetTagDouble();
//  		// rxPower = TxPower-loss
//  		double loss = m_txPowerBaseDbm-rxPowerDbm;
//  		//newTxPower = EndThreshold + loss + margin
//  		double newTxPower = loss + WToDbm(m_edThresholdW);
//  		if (newTxPower > m_txPowerBaseDbm){
//  			newTxPower = m_txPowerBaseDbm;
//  		}
////  		std::cout<<rxPowerDbm<<" TRansmito con la potencia modificada  "<<newTxPower<< " "<<loss <<std::endl;
//  		m_channel->Send (this, packet, newTxPower + m_txGainDb, txMode, preamble);
//  		//Get the global value for the total energy used
////  		double energy_used = newTxPower;
////  		Iterator i = GlobalValue::Begin();
////  		while ( (*i)->GetName() != "EnergyUsed"){
////  			i++;
////  		}
////  		//AttributeValue aw;
////  		DoubleValue aw;
////  		(*i)->GetValue (aw);
////  		energy_used +=aw.Get();
////  		(*i)->SetValue(DoubleValue(energy_used));
//  	}
//  	else{
// // 		std::cout<<"TRansmito con la potencia por defecto  "<<std::endl;
//  		m_channel->Send (this, packet, GetPowerDbm (txPower) + m_txGainDb, txMode, preamble);
//  	}
//
//
////  double newTxPow = 2.0;
// // std::cout<<"La potencia que introduzco ess    "<<newTxPow<<std::endl;
////  m_channel->Send (this, packet, newTxPow + m_txGainDb, txMode, preamble);
// // m_channel->Send (this, packet, GetPowerDbm (txPower) + m_txGainDb, txMode, preamble);
//}

uint32_t 
OvnisWifiPhy::GetNModes (void) const
{
  return m_deviceRateSet.size ();
}
WifiMode 
OvnisWifiPhy::GetMode (uint32_t mode) const
{
  return m_deviceRateSet[mode];
}
uint32_t 
OvnisWifiPhy::GetNTxPower (void) const
{
  return m_nTxPower;
}

void
OvnisWifiPhy::Configure80211a (void)
{
  NS_LOG_FUNCTION (this);
  m_channelStartingFrequency = 5e3; // 5.000 GHz 

  m_deviceRateSet.push_back (WifiPhy::GetOfdmRate6Mbps ());
  m_deviceRateSet.push_back (WifiPhy::GetOfdmRate9Mbps ());
  m_deviceRateSet.push_back (WifiPhy::GetOfdmRate12Mbps ());
  m_deviceRateSet.push_back (WifiPhy::GetOfdmRate18Mbps ());
  m_deviceRateSet.push_back (WifiPhy::GetOfdmRate24Mbps ());
  m_deviceRateSet.push_back (WifiPhy::GetOfdmRate36Mbps ());
  m_deviceRateSet.push_back (WifiPhy::GetOfdmRate48Mbps ());
  m_deviceRateSet.push_back (WifiPhy::GetOfdmRate54Mbps ());
}


void
OvnisWifiPhy::Configure80211b (void)
{
  NS_LOG_FUNCTION (this);
  m_channelStartingFrequency = 2407; // 2.407 GHz

  m_deviceRateSet.push_back (WifiPhy::GetDsssRate1Mbps ());
  m_deviceRateSet.push_back (WifiPhy::GetDsssRate2Mbps ());
  m_deviceRateSet.push_back (WifiPhy::GetDsssRate5_5Mbps ());
  m_deviceRateSet.push_back (WifiPhy::GetDsssRate11Mbps ());
}

void
OvnisWifiPhy::Configure80211g (void)
{
  NS_LOG_FUNCTION (this);
  m_channelStartingFrequency = 2407; // 2.407 GHz

  m_deviceRateSet.push_back (WifiPhy::GetDsssRate1Mbps ());
  m_deviceRateSet.push_back (WifiPhy::GetDsssRate2Mbps ());
  m_deviceRateSet.push_back (WifiPhy::GetDsssRate5_5Mbps ());
  m_deviceRateSet.push_back (WifiPhy::GetErpOfdmRate6Mbps ());
  m_deviceRateSet.push_back (WifiPhy::GetErpOfdmRate9Mbps ());
  m_deviceRateSet.push_back (WifiPhy::GetDsssRate11Mbps ());
  m_deviceRateSet.push_back (WifiPhy::GetErpOfdmRate12Mbps ());
  m_deviceRateSet.push_back (WifiPhy::GetErpOfdmRate18Mbps ());
  m_deviceRateSet.push_back (WifiPhy::GetErpOfdmRate24Mbps ());
  m_deviceRateSet.push_back (WifiPhy::GetErpOfdmRate36Mbps ());
  m_deviceRateSet.push_back (WifiPhy::GetErpOfdmRate48Mbps ());
  m_deviceRateSet.push_back (WifiPhy::GetErpOfdmRate54Mbps ());
}

void
OvnisWifiPhy::Configure80211_10Mhz (void)
{
  NS_LOG_FUNCTION (this);
  m_channelStartingFrequency = 5e3; // 5.000 GHz, suppose 802.11a 

  m_deviceRateSet.push_back (WifiPhy::GetOfdmRate3MbpsBW10MHz ());
  m_deviceRateSet.push_back (WifiPhy::GetOfdmRate4_5MbpsBW10MHz ());
  m_deviceRateSet.push_back (WifiPhy::GetOfdmRate6MbpsBW10MHz ());
  m_deviceRateSet.push_back (WifiPhy::GetOfdmRate9MbpsBW10MHz ());
  m_deviceRateSet.push_back (WifiPhy::GetOfdmRate12MbpsBW10MHz ());
  m_deviceRateSet.push_back (WifiPhy::GetOfdmRate18MbpsBW10MHz ());
  m_deviceRateSet.push_back (WifiPhy::GetOfdmRate24MbpsBW10MHz ());
  m_deviceRateSet.push_back (WifiPhy::GetOfdmRate27MbpsBW10MHz ());
}

void
OvnisWifiPhy::Configure80211_5Mhz (void)
{
  NS_LOG_FUNCTION (this); 
  m_channelStartingFrequency = 5e3; // 5.000 GHz, suppose 802.11a

  m_deviceRateSet.push_back (WifiPhy::GetOfdmRate1_5MbpsBW5MHz ());
  m_deviceRateSet.push_back (WifiPhy::GetOfdmRate2_25MbpsBW5MHz ());
  m_deviceRateSet.push_back (WifiPhy::GetOfdmRate3MbpsBW5MHz ());
  m_deviceRateSet.push_back (WifiPhy::GetOfdmRate4_5MbpsBW5MHz ());
  m_deviceRateSet.push_back (WifiPhy::GetOfdmRate6MbpsBW5MHz ());
  m_deviceRateSet.push_back (WifiPhy::GetOfdmRate9MbpsBW5MHz ());
  m_deviceRateSet.push_back (WifiPhy::GetOfdmRate12MbpsBW5MHz ());
  m_deviceRateSet.push_back (WifiPhy::GetOfdmRate13_5MbpsBW5MHz ());
}

void
OvnisWifiPhy::ConfigureHolland (void)
{
  NS_LOG_FUNCTION (this);
  m_channelStartingFrequency = 5e3; // 5.000 GHz 
  m_deviceRateSet.push_back (WifiPhy::GetOfdmRate6Mbps ());
  m_deviceRateSet.push_back (WifiPhy::GetOfdmRate12Mbps ());
  m_deviceRateSet.push_back (WifiPhy::GetOfdmRate18Mbps ());
  m_deviceRateSet.push_back (WifiPhy::GetOfdmRate36Mbps ());
  m_deviceRateSet.push_back (WifiPhy::GetOfdmRate54Mbps ());
}

void
OvnisWifiPhy::Configure80211p_CCH (void)
{
  NS_LOG_FUNCTION (this);
  m_channelStartingFrequency = 5e3; // 802.11p works over the 5Ghz freq range

  m_deviceRateSet.push_back (WifiPhy::GetOfdmRate3MbpsBW10MHz ());
  m_deviceRateSet.push_back (WifiPhy::GetOfdmRate4_5MbpsBW10MHz ());
  m_deviceRateSet.push_back (WifiPhy::GetOfdmRate6MbpsBW10MHz ());
  m_deviceRateSet.push_back (WifiPhy::GetOfdmRate9MbpsBW10MHz ());
  m_deviceRateSet.push_back (WifiPhy::GetOfdmRate12MbpsBW10MHz ());
  m_deviceRateSet.push_back (WifiPhy::GetOfdmRate18MbpsBW10MHz ());
  m_deviceRateSet.push_back (WifiPhy::GetOfdmRate24MbpsBW10MHz ());
  m_deviceRateSet.push_back (WifiPhy::GetOfdmRate27MbpsBW10MHz ());
}

void
OvnisWifiPhy::Configure80211p_SCH (void)
{
  NS_LOG_FUNCTION (this);
  m_channelStartingFrequency = 5e3; // 802.11p works over the 5Ghz freq range

  m_deviceRateSet.push_back (WifiPhy::GetOfdmRate3MbpsBW10MHz ());
  m_deviceRateSet.push_back (WifiPhy::GetOfdmRate4_5MbpsBW10MHz ());
  m_deviceRateSet.push_back (WifiPhy::GetOfdmRate6MbpsBW10MHz ());
  m_deviceRateSet.push_back (WifiPhy::GetOfdmRate9MbpsBW10MHz ());
  m_deviceRateSet.push_back (WifiPhy::GetOfdmRate12MbpsBW10MHz ());
  m_deviceRateSet.push_back (WifiPhy::GetOfdmRate18MbpsBW10MHz ());
  m_deviceRateSet.push_back (WifiPhy::GetOfdmRate24MbpsBW10MHz ());
  m_deviceRateSet.push_back (WifiPhy::GetOfdmRate27MbpsBW10MHz ());
}

void 
OvnisWifiPhy::RegisterListener (WifiPhyListener *listener)
{
  m_state->RegisterListener (listener);
}

bool 
OvnisWifiPhy::IsStateCcaBusy (void)
{
  return m_state->IsStateCcaBusy ();
}

bool 
OvnisWifiPhy::IsStateIdle (void)
{
  return m_state->IsStateIdle ();
}
bool 
OvnisWifiPhy::IsStateBusy (void)
{
  return m_state->IsStateBusy ();
}
bool 
OvnisWifiPhy::IsStateRx (void)
{
  return m_state->IsStateRx ();
}
bool 
OvnisWifiPhy::IsStateTx (void)
{
  return m_state->IsStateTx ();
}
bool 
OvnisWifiPhy::IsStateSwitching (void)
{
  return m_state->IsStateSwitching ();
}

Time
OvnisWifiPhy::GetStateDuration (void)
{
  return m_state->GetStateDuration ();
}
Time
OvnisWifiPhy::GetDelayUntilIdle (void)
{
  return m_state->GetDelayUntilIdle ();
}

Time 
OvnisWifiPhy::GetLastRxStartTime (void) const
{
  return m_state->GetLastRxStartTime ();
}

//Time
//OvnisWifiPhy::CalculateTxDuration (uint32_t size, WifiMode payloadMode, enum WifiPreamble preamble) const
//{
//  return m_interference.CalculateTxDuration (size, payloadMode, preamble);
//}

double 
OvnisWifiPhy::DbToRatio (double dB) const
{
  double ratio = pow(10.0,dB/10.0);
  return ratio;
}

double 
OvnisWifiPhy::DbmToW (double dBm) const
{
  double mW = pow(10.0,dBm/10.0);
  return mW / 1000.0;
}

double
OvnisWifiPhy::WToDbm (double w) const
{
  return 10.0 * log10(w * 1000.0);
}

double
OvnisWifiPhy::RatioToDb (double ratio) const
{
  return 10.0 * log10(ratio);
}

double
OvnisWifiPhy::GetEdThresholdW (void) const
{
  return m_edThresholdW;
}

double 
OvnisWifiPhy::GetPowerDbm (uint8_t power) const
{
  NS_ASSERT (m_txPowerBaseDbm <= m_txPowerEndDbm);
  NS_ASSERT (m_nTxPower > 0);
  //double dbm = m_txPowerBaseDbm + power * (m_txPowerEndDbm - m_txPowerBaseDbm) / m_nTxPower;
   double dbm;
  if (m_nTxPower > 1)
    {
      dbm = m_txPowerBaseDbm + power * (m_txPowerEndDbm - m_txPowerBaseDbm) / (m_nTxPower - 1);
    }
  else 
    {
      NS_ASSERT_MSG (m_txPowerBaseDbm == m_txPowerEndDbm, "cannot have TxPowerEnd != TxPowerStart with TxPowerLevels == 1");
      dbm = m_txPowerBaseDbm;
    }
	return dbm;
}

void
OvnisWifiPhy::EndReceive (Ptr<Packet> packet, Ptr<InterferenceHelper::Event> event)
{
  NS_LOG_FUNCTION (this << packet << event);
  NS_ASSERT (IsStateRx ());
  NS_ASSERT (event->GetEndTime () == Simulator::Now ());

  struct InterferenceHelper::SnrPer snrPer;
  snrPer = m_interference.CalculateSnrPer (event);
  m_interference.NotifyRxEnd();

  NS_LOG_DEBUG ("mode="<<(event->GetPayloadMode ().GetDataRate ())<<
                ", snr="<<snrPer.snr<<", per="<<snrPer.per<<", size="<<packet->GetSize ());
  if (m_random->GetValue () > snrPer.per)
    {
      NotifyRxEnd (packet); 
      uint32_t dataRate500KbpsUnits = event->GetPayloadMode ().GetDataRate () / 500000;   
      bool isShortPreamble = (WIFI_PREAMBLE_SHORT == event->GetPreambleType ());  
      double signalDbm = RatioToDb (event->GetRxPowerW ()) + 30;
      double noiseDbm = RatioToDb(event->GetRxPowerW() / snrPer.snr) - GetRxNoiseFigure() + 30 ;
      //NotifyPromiscSniffRx (packet, (uint16_t)GetChannelFrequencyMhz (), GetChannelNumber (), dataRate500KbpsUnits, isShortPreamble, signalDbm, noiseDbm);
      NotifyMonitorSniffRx (packet, (uint16_t)GetChannelFrequencyMhz (), GetChannelNumber (), dataRate500KbpsUnits, isShortPreamble, signalDbm, noiseDbm);
      m_state->SwitchFromRxEndOk (packet, snrPer.snr, event->GetPayloadMode (), event->GetPreambleType ());
    } 
  else 
    {
      /* failure. */
      NotifyRxDrop (packet);
      m_state->SwitchFromRxEndError (packet, snrPer.snr);
    }
}


// Added by Patricia Ruiz (eg of received messages)
void
OvnisWifiPhy::SetRxPowerDBm (double level){
	m_rxPowerDbm = level;
}
double
OvnisWifiPhy::GetRxPowerDBm (){
	return m_rxPowerDbm;
}

//aadded AgataGrzybek (ns3 3.16)
int64_t OvnisWifiPhy::AssignStreams (int64_t stream)
{
  NS_LOG_FUNCTION (this << stream);
  m_random->SetStream (stream);
  return 1;
}

uint32_t
OvnisWifiPhy::GetNBssMembershipSelectors (void) const
{
  return  m_bssMembershipSelectorSet.size ();
}
uint32_t
OvnisWifiPhy::GetBssMembershipSelector (uint32_t selector) const
{
  return  m_bssMembershipSelectorSet[selector];
}

WifiModeList
OvnisWifiPhy::GetMembershipSelectorModes(uint32_t selector)
{
  uint32_t id=GetBssMembershipSelector(selector);
  WifiModeList supportedmodes;
  if (id == HT_PHY)
  {
    //mandatory MCS 0 to 7
     supportedmodes.push_back (WifiPhy::GetOfdmRate6_5MbpsBW20MHz ());
     supportedmodes.push_back (WifiPhy::GetOfdmRate13MbpsBW20MHz ());
     supportedmodes.push_back (WifiPhy::GetOfdmRate19_5MbpsBW20MHz ());
     supportedmodes.push_back (WifiPhy::GetOfdmRate26MbpsBW20MHz ());
     supportedmodes.push_back (WifiPhy::GetOfdmRate39MbpsBW20MHz ());
     supportedmodes.push_back (WifiPhy::GetOfdmRate52MbpsBW20MHz ());
     supportedmodes.push_back (WifiPhy::GetOfdmRate58_5MbpsBW20MHz ());
     supportedmodes.push_back (WifiPhy::GetOfdmRate65MbpsBW20MHz ());
  }
  return supportedmodes;
}
uint8_t
OvnisWifiPhy::GetNMcs (void) const
{
  return  m_deviceMcsSet.size ();
}
uint8_t
OvnisWifiPhy::GetMcs (uint8_t mcs) const
{
  return  m_deviceMcsSet[mcs];
}
uint32_t
OvnisWifiPhy::WifiModeToMcs (WifiMode mode)
{
    uint32_t mcs = 0;
   if (mode.GetUniqueName() == "OfdmRate135MbpsBW40MHzShGi" || mode.GetUniqueName() == "OfdmRate65MbpsBW20MHzShGi" )
     {
             mcs=6;
     }
  else
    {
     switch (mode.GetDataRate())
       {
         case 6500000:
         case 7200000:
         case 13500000:
         case 15000000:
           mcs=0;
           break;
         case 13000000:
         case 14400000:
         case 27000000:
         case 30000000:
           mcs=1;
           break;
         case 19500000:
         case 21700000:
         case 40500000:
         case 45000000:
           mcs=2;
           break;
         case 26000000:
         case 28900000:
         case 54000000:
         case 60000000:
           mcs=3;
           break;
         case 39000000:
         case 43300000:
         case 81000000:
         case 90000000:
           mcs=4;
           break;
         case 52000000:
         case 57800000:
         case 108000000:
         case 120000000:
           mcs=5;
           break;
         case 58500000:
         case 121500000:
           mcs=6;
           break;
         case 65000000:
         case 72200000:
         case 135000000:
         case 150000000:
           mcs=7;
           break;
       }
    }
  return mcs;
}
WifiMode
OvnisWifiPhy::McsToWifiMode (uint8_t mcs)
{
   WifiMode mode;
   switch (mcs)
     {
       case 7:
          if (!GetGuardInterval() && !GetChannelBonding())
           {
              mode =  WifiPhy::GetOfdmRate65MbpsBW20MHz ();
            }
         else if(GetGuardInterval() && !GetChannelBonding())
            {
              mode = WifiPhy::GetOfdmRate72_2MbpsBW20MHz ();
            }
          else if (!GetGuardInterval() && GetChannelBonding())
            {
              mode = WifiPhy::GetOfdmRate135MbpsBW40MHz ();
            }
          else
            {
              mode = WifiPhy::GetOfdmRate150MbpsBW40MHz ();
            }
          break;
       case 6:
          if (!GetGuardInterval() && !GetChannelBonding())
           {
              mode = WifiPhy::GetOfdmRate58_5MbpsBW20MHz ();

            }
         else if(GetGuardInterval() && !GetChannelBonding())
            {
              mode =  WifiPhy::GetOfdmRate65MbpsBW20MHzShGi ();

            }
          else if (!GetGuardInterval() && GetChannelBonding())
            {
              mode = WifiPhy::GetOfdmRate121_5MbpsBW40MHz ();

            }
          else
            {
              mode= WifiPhy::GetOfdmRate135MbpsBW40MHzShGi ();

            }
          break;
       case 5:
          if (!GetGuardInterval() && !GetChannelBonding())
           {
              mode = WifiPhy::GetOfdmRate52MbpsBW20MHz ();

            }
         else if(GetGuardInterval() && !GetChannelBonding())
            {
              mode = WifiPhy::GetOfdmRate57_8MbpsBW20MHz ();
            }
          else if (!GetGuardInterval() && GetChannelBonding())
            {
              mode = WifiPhy::GetOfdmRate108MbpsBW40MHz ();

            }
          else
            {
              mode = WifiPhy::GetOfdmRate120MbpsBW40MHz ();

            }
          break;
       case 4:
          if (!GetGuardInterval() && !GetChannelBonding())
           {
              mode = WifiPhy::GetOfdmRate39MbpsBW20MHz ();
            }
         else if(GetGuardInterval() && !GetChannelBonding())
            {
              mode = WifiPhy::GetOfdmRate43_3MbpsBW20MHz ();
            }
          else if (!GetGuardInterval() && GetChannelBonding())
            {
              mode = WifiPhy::GetOfdmRate81MbpsBW40MHz ();

            }
          else
            {
              mode = WifiPhy::GetOfdmRate90MbpsBW40MHz ();

            }
          break;
       case 3:
          if (!GetGuardInterval() && !GetChannelBonding())
           {
              mode =  WifiPhy::GetOfdmRate26MbpsBW20MHz ();

            }
         else if(GetGuardInterval() && !GetChannelBonding())
            {
              mode = WifiPhy::GetOfdmRate28_9MbpsBW20MHz ();

            }
          else if (!GetGuardInterval() && GetChannelBonding())
            {
              mode = WifiPhy::GetOfdmRate54MbpsBW40MHz ();

            }
          else
            {
              mode = WifiPhy::GetOfdmRate60MbpsBW40MHz ();
            }
          break;
       case 2:
          if (!GetGuardInterval() && !GetChannelBonding())
           {
              mode = WifiPhy::GetOfdmRate19_5MbpsBW20MHz ();

            }
         else if(GetGuardInterval() && !GetChannelBonding())
            {
              mode = WifiPhy::GetOfdmRate21_7MbpsBW20MHz ();

            }
          else if (!GetGuardInterval() && GetChannelBonding())
            {
              mode =  WifiPhy::GetOfdmRate40_5MbpsBW40MHz ();

            }
          else
            {
              mode = WifiPhy::GetOfdmRate45MbpsBW40MHz ();

            }
          break;
       case 1:
          if (!GetGuardInterval() && !GetChannelBonding())
           {
            mode = WifiPhy::GetOfdmRate13MbpsBW20MHz ();

            }
         else if(GetGuardInterval() && !GetChannelBonding())
            {
              mode =  WifiPhy::GetOfdmRate14_4MbpsBW20MHz ();
            }
          else if (!GetGuardInterval() && GetChannelBonding())
            {
              mode = WifiPhy::GetOfdmRate27MbpsBW40MHz ();

            }
          else
            {
              mode = WifiPhy::GetOfdmRate30MbpsBW40MHz ();
            }
          break;
       case 0:
       default:
         if (!GetGuardInterval() && !GetChannelBonding())
           {
              mode = WifiPhy::GetOfdmRate6_5MbpsBW20MHz ();

            }
         else if(GetGuardInterval() && !GetChannelBonding())
            {
              mode = WifiPhy::GetOfdmRate7_2MbpsBW20MHz ();
            }
          else if (!GetGuardInterval() && GetChannelBonding())
            {
              mode = WifiPhy::GetOfdmRate13_5MbpsBW40MHz ();

            }
          else
            {
              mode = WifiPhy::GetOfdmRate15MbpsBW40MHz ();
            }
         break;
        }
 return mode;
}

void
OvnisWifiPhy::SetFrequency (uint32_t freq)
{
  m_channelStartingFrequency = freq;
}

void
OvnisWifiPhy::SetNumberOfTransmitAntennas (uint32_t tx)
{
  m_numberOfTransmitters = tx;
}
void
OvnisWifiPhy::SetNumberOfReceiveAntennas (uint32_t rx)
{
  m_numberOfReceivers = rx;
}

void
OvnisWifiPhy::SetLdpc (bool Ldpc)
{
  m_ldpc = Ldpc;
}

void
OvnisWifiPhy::SetStbc (bool stbc)
{
  m_stbc = stbc;
}

void
OvnisWifiPhy::SetGreenfield (bool greenfield)
{
  m_greenfield = greenfield;
}
bool
OvnisWifiPhy::GetGuardInterval (void) const
{
  return m_guardInterval;
}
void
OvnisWifiPhy::SetGuardInterval (bool GuardInterval)
{
  m_guardInterval = GuardInterval;
}

uint32_t
OvnisWifiPhy::GetFrequency (void) const
{
  return m_channelStartingFrequency;
}

uint32_t
OvnisWifiPhy::GetNumberOfTransmitAntennas (void) const
{
  return m_numberOfTransmitters;
}
uint32_t
OvnisWifiPhy::GetNumberOfReceiveAntennas (void) const
{
  return m_numberOfReceivers;
}

bool
OvnisWifiPhy::GetLdpc (void) const
{
  return m_ldpc;
}
bool
OvnisWifiPhy::GetStbc (void) const
{
  return m_stbc;
}

bool
OvnisWifiPhy::GetGreenfield (void) const
{
  return m_greenfield;
}

bool
OvnisWifiPhy::GetChannelBonding(void) const
{
  return m_channelBonding;
}

void
OvnisWifiPhy::SetChannelBonding(bool channelbonding)
{
  m_channelBonding= channelbonding;
}



} // namespace ns3
