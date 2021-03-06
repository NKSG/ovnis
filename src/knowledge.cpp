/*
 * knowledge.cpp
 *
 *  Created on: Jan 31, 2013
 *      Author: agata
 */

#include "knowledge.h"
#include "traci/structs.h"

using namespace std;

namespace ovnis {

Knowledge::Knowledge() {
	maxInformationAge = PACKET_TTL;
	congestionThreshold = CONGESTION_THRESHOLD;
	densityThreshold = DENSITY_THRESHOLD;

	traci = Names::Find<ovnis::SumoTraciConnection>("SumoTraci");
}

Knowledge::~Knowledge() {
}

int Knowledge::getNumberOfVehicles(std::string edgeId) {
	int vehs = 0;
	if (numberOfVehicles.find(edgeId) != numberOfVehicles.end()) {
		vehs = numberOfVehicles[edgeId];
	}
	return vehs;
}

int Knowledge::addNumberOfVehicles(std::string edgeId) {
	if (numberOfVehicles.find(edgeId) != numberOfVehicles.end()) {
		++numberOfVehicles[edgeId];
	}
	else {
		numberOfVehicles[edgeId] = 1;
	}
	return numberOfVehicles[edgeId];
}

int Knowledge::substractNumberOfVehicles(std::string edgeId) {
	if (numberOfVehicles.find(edgeId) != numberOfVehicles.end()) {
		numberOfVehicles[edgeId] = numberOfVehicles[edgeId]==0 ? 0 : --numberOfVehicles[edgeId];
	}
	return numberOfVehicles[edgeId];
}

bool Knowledge::record(Data data) {
	// record only information that is fresher than the last heard
	if (travelTimes.find(data.edgeId) == travelTimes.end() ||
			(travelTimes.find(data.edgeId) != travelTimes.end() && travelTimes[data.edgeId].getLatestTime() < data.date)) {
		travelTimes[data.edgeId].add(0, "", data.date, data.travelTime);
		return true;
	}
	// ?????
//	// record if travel time is longer than last heard
//	if (travelTimes.find(data.edgeId) == travelTimes.end() ||
//			(travelTimes.find(data.edgeId) != travelTimes.end() && travelTimes[data.edgeId].getLatestValue() < data.travelTime)) {
//		travelTimes[data.edgeId].add(0, "", data.date, data.travelTime);
//		return true;
//	}

	return false;
}

void Knowledge::record(vector<Data> data) {
	for (vector<Data>::iterator it = data.begin(); it != data.end(); ++it) {
		record(*it);
	}
}

map<string,RecordEntry> & Knowledge::getRecords()
{
	return travelTimes;
}

void Knowledge::analyseLocalDatabase(map<string, Route> routes, string startEdgeId, string endEdgeId, map<string,double> routeTTL, bool usePerfectInformation) {
	ifCongestedFlow = false;
	ifDenseFlow = false;
	sumLength = 0;
	congestedLength = 0;
	denseLength = 0;
	sumDelay = 0;
	int totalNumberOfUpdatedEdges = 0;
	map<string,int> vehicles;
	map<string,double> newTravelTimesOnRoutes = map<string,double>(); // for a potential comparison to the last state in the local data base
	packetAgesOnRoutes = map<string,double>();
	delayOnRoutes = map<string,double>();
	congestedLengthOnRoutes = map<string,double>();
	denseLengthOnRoutes = map<string,double>();
	// initialize
	for (map<string, Route>::iterator it = routes.begin(); it != routes.end(); ++it) {
		newTravelTimesOnRoutes[it->first] = TIS::getInstance().computeStaticCostExcludingMargins(it->first, startEdgeId, endEdgeId);
		packetAgesOnRoutes[it->first] = 0;
		vehicles[it->first] = 0;
		numberOfUpdatedEdges[it->first] = 0;
		numberOfEdges[it->first] = 0;
		congestedLengthOnRoutes[it->first] = 0;
		denseLengthOnRoutes[it->first] = 0;
		delayOnRoutes[it->first] = 0;
	}

//	map<string, RecordEntry> perfectDB = TIS::getInstance().getPerfectTravelTimes();

	for (map<string, RecordEntry>::iterator it = travelTimes.begin(); it != travelTimes.end(); ++it) {
		RecordEntry recordEntry = it->second;
		string edgeId = it->first;
		double travelTime = recordEntry.getLatestValue();
		double packetDate = recordEntry.getLatestTime();
		double packetAge = packetDate == 0 ? 0 : Simulator::Now().GetSeconds() - packetDate;
//		double avgTravelTime = it->second.getAverageValue();
//		double avgPacketDate = it->second.getAverageTime();
//		travelTime = avgTravelTime;
//		packetDate = avgPacketDate;

		if (usePerfectInformation) {
			Log::getInstance().getStream("perfect") << Simulator::Now().GetSeconds() << "\t" << edgeId << "\tvanet\t" << travelTime << "\t" << packetAge;
			travelTime = TIS::getInstance().getEdgePerfectCost(edgeId);
			packetDate = Simulator::Now().GetSeconds();
			packetAge = 0;
			travelTimes[edgeId].add(0, "", packetDate, travelTime);
			Log::getInstance().getStream("perfect") << Simulator::Now().GetSeconds() << "\tperfect\t" << travelTime << "\t" << packetAge << endl;
		}
		double staticCost = TIS::getInstance().getEdgeStaticCost(edgeId);
		if (staticCost != 0) {
			recordEntry.setCapacity(staticCost);
		}
		int vehs = numberOfVehicles[it->first];

		// calculate travel times on routes
		for (map<string, Route>::iterator itRoutes = routes.begin(); itRoutes != routes.end(); ++itRoutes) {
			if (itRoutes->second.containsEdgeExcludedMargins(edgeId, startEdgeId, endEdgeId)) { // if the edge belongs to the part of the future route
//				maxInformationAge = routeTTL[itRoutes->first];
				maxInformationAge = 120;
				double edgeLength = TIS::getInstance().getEdgeLength(edgeId);
				sumLength += edgeLength;
				++numberOfEdges[itRoutes->first];
				if (travelTime > 0 && packetAge < maxInformationAge) { // information was recorded about this edge and is fresh enough
					// update travel time with the information from th elocal database
					newTravelTimesOnRoutes[itRoutes->first] = newTravelTimesOnRoutes[itRoutes->first] - staticCost + travelTime;
					++numberOfUpdatedEdges[itRoutes->first];
					++totalNumberOfUpdatedEdges;
					Log::getInstance().getStream("capacity") << Simulator::Now().GetSeconds() << "\t";
					if (travelTime != SIMULATION_STEP_INTERVAL || recordEntry.getExpectedValue() > SIMULATION_STEP_INTERVAL) { // sumo bug fix - travelTime > 1 means that it was recorded in sumo!
						double delay = travelTime - recordEntry.getExpectedValue();
						if (delay > 0) {
							delayOnRoutes[itRoutes->first] += delay;
							sumDelay += delay;
						}
						Log::getInstance().getStream("vanets_knowledge") << edgeId << "," << packetDate << "," << travelTime  <<"," << recordEntry.getExpectedValue() << "\t";
						Log::getInstance().getStream("delay") << Simulator::Now().GetSeconds() << "\t" << edgeId << "\t" << travelTime << "\t-\t" << recordEntry.getExpectedValue() << "\t=\t" << delay << "\t";
						Log::getInstance().getStream("capacity") << edgeId << "," << recordEntry.getExpectedValue() << "," << travelTime << "," << recordEntry.getActualCapacity() << "\t";
						if (recordEntry.getActualCapacity() < congestionThreshold) {
							congestedLength += edgeLength;
							ifCongestedFlow = true;
							double now = Simulator::Now().GetSeconds();
							congestedLengthOnRoutes[itRoutes->first] += edgeLength;
							Log::getInstance().getStream("congestion") << Simulator::Now().GetSeconds() << "\t" << edgeId << "\t" << recordEntry.getActualCapacity() << "<" << congestionThreshold << "\t" << travelTime << "\t" << recordEntry.getExpectedValue() << endl;
						}
						if (recordEntry.getActualCapacity() > congestionThreshold && recordEntry.getActualCapacity() < densityThreshold) {
							ifDenseFlow = true;
							denseLengthOnRoutes[itRoutes->first] += edgeLength;
							Log::getInstance().getStream("dense") << Simulator::Now().GetSeconds() << "\t" << edgeId << "\t" << congestionThreshold << "<" << recordEntry.getActualCapacity() << "<" << densityThreshold << "\t" << travelTime << "\t" << recordEntry.getExpectedValue() << endl;
						}
					}
				}
				else if (travelTime > 0 && packetAge < maxInformationAge) { // information was recorded but it's older than the allowed treshold
					// reset packet age
					packetAge = 0;
				}
				packetAgesOnRoutes[itRoutes->first] += packetAge;
			}
		}
	}

	// calculate the average information age
	for (map<string, Route>::iterator it = routes.begin(); it != routes.end(); ++it) {
		packetAgesOnRoutes[it->first] = numberOfUpdatedEdges[it->first] == 0 ? 0 : packetAgesOnRoutes[it->first] / numberOfUpdatedEdges[it->first];
	}
	// print knowledge
	for (map<string, Route>::iterator it = routes.begin(); it != routes.end(); ++it) {
		packetAgesOnRoutes[it->first] = numberOfUpdatedEdges[it->first] == 0 ? 0 : packetAgesOnRoutes[it->first] / numberOfUpdatedEdges[it->first];
		//if (totalNumberOfUpdatedEdges > 0) {
			Log::getInstance().getStream("vanets_knowledge") << it->first << "," << numberOfUpdatedEdges[it->first] << "," << it->second.countEdgesExcludedMargins(startEdgeId, endEdgeId) << "\t";
		//}
	}
	Log::getInstance().getStream("vanets_knowledge") << endl;
//	for (map<string, double>::iterator it = newTravelTimesOnRoutes.begin(); it != newTravelTimesOnRoutes.end(); ++it) {
////		cout << "route " << it->first << ", new travel time " << it->second << ", traveltime " << travelTimesOnRoutes[it->first] << ", number of updated edges: " << numberOfUpdatedEdges[it->first] << " / number of edges: " << numberOfEdges[it->first] << endl;
//	}
	travelTimesOnRoutes = newTravelTimesOnRoutes;
}


map<string,map<string,vector<string> > > Knowledge::analyseCorrelation(map<string, Route> routes, string startEdgeId, string endEdgeId) {
	map<string,map<string,vector<string> > > correlated;
	for (map<string, Route>::iterator itRoutes = routes.begin(); itRoutes != routes.end(); ++itRoutes) {
		correlated[itRoutes->first] = map<string,vector<string> >();
		for (map<string, Route>::iterator itRoutesCorrleated = routes.begin(); itRoutesCorrleated != routes.end(); ++itRoutesCorrleated) {
			if (itRoutes->first != itRoutesCorrleated->first) {
				correlated[itRoutes->first][itRoutesCorrleated->first] = vector<string>();
				for (vector<string>::iterator itEdges = itRoutes->second.getEdgeIds().begin(); itEdges != itRoutes->second.getEdgeIds().end(); ++itEdges) {
					if (itRoutesCorrleated->second.containsEdgeExcludedMargins(*itEdges, startEdgeId, endEdgeId)) {
						correlated[itRoutes->first][itRoutesCorrleated->first].push_back(*itEdges);
					}
				}
			}
		}
	}
	if (TIS::getInstance().executeOnce == false) {
		for (map<string, map<string,vector<string> > >::iterator itCorrelated = correlated.begin(); itCorrelated != correlated.end(); ++itCorrelated) {
			Log::getInstance().getStream("correlation") << itCorrelated->first << ":" << endl;
			for (map<string,vector<string> >::iterator itCorrelatedTo = itCorrelated->second.begin(); itCorrelatedTo != itCorrelated->second.end(); ++itCorrelatedTo) {
				Log::getInstance().getStream("correlation") << itCorrelatedTo->first << "\t" << itCorrelatedTo->second.size() << ":\t";
				for (vector<string>::iterator it = itCorrelatedTo->second.begin(); it != itCorrelatedTo->second.end(); ++it) {
					Log::getInstance().getStream("correlation") << *it << "\t";
				}
				Log::getInstance().getStream("correlation") << endl;
			}
			Log::getInstance().getStream("correlation") << endl;
		}
		TIS::getInstance().executeOnce = true;
	}
	return correlated;
}

map<string,double> Knowledge::getTravelTimesOnRoutes() {
	return travelTimesOnRoutes;
}

map<string,double> Knowledge::getDelayOnRoutes() {
	return delayOnRoutes;
}

map<string,double> Knowledge::getCongestedLengthOnRoutes() {
	return congestedLengthOnRoutes;
}

map<string,double> Knowledge::getDenseLengthOnRoutes() {
	return denseLengthOnRoutes;
}

double Knowledge::getSumDelay() {
	return sumDelay;
}

double Knowledge::getSumCongested() {
	return congestedLength;
}

double Knowledge::getSumDensed() {
	return denseLength;
}

double Knowledge::getSumLength() {
	return sumLength;
}

map<std::string,double> Knowledge::getEdgesCosts(map<string, Route> routes, string startEdgeId, string endEdgeId, bool usePerfect)
{
	map<string, RecordEntry> ttdb = travelTimes;
	if (usePerfect) {
		ttdb = TIS::getInstance().getPerfectTravelTimes();
	}
	map<std::string, double> edgesCosts;//edgesCosts = map<string, double> ();
	for (map<string, EdgeInfo>::iterator it = TIS::getInstance().getStaticRecords().begin(); it != TIS::getInstance().getStaticRecords().end(); ++it) {
		for (map<string, Route>::iterator itRoutes = routes.begin(); itRoutes != routes.end(); ++itRoutes) {
			if (itRoutes->second.containsEdgeExcludedMargins(it->first, startEdgeId, endEdgeId)) {
				if (edgesCosts.find(it->first) == edgesCosts.end()) {
					double travelTime = ttdb[it->first].getLatestValue();
					double packetAge = ttdb[it->first].getLatestTime() == 0 ? 0 : Simulator::Now().GetSeconds() - ttdb[it->first].getLatestTime();
					if (travelTime > 0 && packetAge < maxInformationAge) { // if the information is fresh enough
						edgesCosts[it->first] = travelTime;
					}
					else {
						edgesCosts[it->first] = it->second.getStaticCost();
					}
				}
			}
		}
	}
    return edgesCosts;
}

map<std::string,double> Knowledge::getEdgesCosts(map<string, Route> routes, string startEdgeId, string endEdgeId)
{
	return getEdgesCosts(routes, startEdgeId, endEdgeId, false);
}

    bool Knowledge::isCongestedFlow() const
    {
        return ifCongestedFlow;
    }

    bool Knowledge::isDenseFlow() const
    {
        return ifDenseFlow;
    }

}
