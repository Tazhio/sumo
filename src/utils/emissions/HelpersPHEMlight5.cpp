/****************************************************************************/
// Eclipse SUMO, Simulation of Urban MObility; see https://eclipse.org/sumo
// Copyright (C) 2013-2022 German Aerospace Center (DLR) and others.
// This program and the accompanying materials are made available under the
// terms of the Eclipse Public License 2.0 which is available at
// https://www.eclipse.org/legal/epl-2.0/
// This Source Code may also be made available under the following Secondary
// Licenses when the conditions for such availability set forth in the Eclipse
// Public License 2.0 are satisfied: GNU General Public License, version 2
// or later which is available at
// https://www.gnu.org/licenses/old-licenses/gpl-2.0-standalone.html
// SPDX-License-Identifier: EPL-2.0 OR GPL-2.0-or-later
/****************************************************************************/
/// @file    HelpersPHEMlight5.cpp
/// @author  Daniel Krajzewicz
/// @author  Michael Behrisch
/// @author  Nikolaus Furian
/// @date    Sat, 20.04.2013
///
// Helper methods for PHEMlight-based emission computation
/****************************************************************************/
#include <config.h>

#include <limits>
#include <cmath>
#include <foreign/PHEMlight/V5/cpp/Constants.h>
#include <utils/common/StringUtils.h>
#include <utils/options/OptionsCont.h>

#include "HelpersPHEMlight5.h"


// ===========================================================================
// method definitions
// ===========================================================================
HelpersPHEMlight5::HelpersPHEMlight5() :
    HelpersPHEMlight("PHEMlight5", PHEMLIGHT5_BASE, -1),
    myIndex(PHEMLIGHT5_BASE) {
}


SUMOEmissionClass
HelpersPHEMlight5::getClassByName(const std::string& eClass, const SUMOVehicleClass vc) {
    if (eClass == "unknown" && !myEmissionClassStrings.hasString("unknown")) {
        myEmissionClassStrings.addAlias("unknown", getClassByName("PC_G_EU4", vc));
    }
    if (eClass == "default" && !myEmissionClassStrings.hasString("default")) {
        myEmissionClassStrings.addAlias("default", getClassByName("PC_G_EU4", vc));
    }
    if (myEmissionClassStrings.hasString(eClass)) {
        return myEmissionClassStrings.get(eClass);
    }
    if (eClass.size() < 6) {
        throw InvalidArgument("Unknown emission class '" + eClass + "'.");
    }
    int index = myIndex++;
    const std::string type = eClass.substr(0, 3);
    if (type == "HDV" || type == "LB_" || type == "RB_" || type == "LSZ" || eClass.find("LKW") != std::string::npos) {
        index |= PollutantsInterface::HEAVY_BIT;
    }
    myEmissionClassStrings.insert(eClass, index);
#ifdef INTERNAL_PHEM
    if (type == "HDV" || type == "LCV" || type == "PC_" || !PHEMCEPHandler::getHandlerInstance().Load(index, eClass)) {
#endif
        std::vector<std::string> phemPath;
        phemPath.push_back(OptionsCont::getOptions().getString("phemlight-path") + "/");
        if (getenv("PHEMLIGHT_PATH") != nullptr) {
            phemPath.push_back(std::string(getenv("PHEMLIGHT_PATH")) + "/");
        }
        if (getenv("SUMO_HOME") != nullptr) {
            phemPath.push_back(std::string(getenv("SUMO_HOME")) + "/data/emissions/PHEMlight/");
        }
        myHelper.setCommentPrefix("c");
        myHelper.setPHEMDataV("V5");
        myHelper.setclass(eClass);
        if (!myCEPHandler.GetCEP(phemPath, &myHelper, nullptr)) {
            myEmissionClassStrings.remove(eClass, index);
            myIndex--;
            throw InvalidArgument("File for PHEM emission class " + eClass + " not found.\n" + myHelper.getErrMsg());
        }
        myCEPs[index] = myCEPHandler.getCEPS().find(myHelper.getgClass())->second;
#ifdef INTERNAL_PHEM
    }
#endif
    myEmissionClassStrings.addAlias(StringUtils::to_lower_case(eClass), index);
    return index;
}


double
HelpersPHEMlight5::getEmission(PHEMlightdllV5::CEP* currCep, const std::string& e, const double p, const double v) const {
    return currCep->GetEmission(e, p, v, &myHelper);
}


double
HelpersPHEMlight5::getModifiedAccel(const SUMOEmissionClass c, const double v, const double a, const double slope) const {
    PHEMlightdllV5::CEP* currCep = myCEPs.count(c) == 0 ? nullptr : myCEPs.find(c)->second;
    if (currCep != nullptr) {
        const bool isHBEV = currCep->getFuelType() == PHEMlightdllV5::Constants::strBEV || currCep->getFuelType() == PHEMlightdllV5::Constants::strHybrid;
        return v == 0.0 ? 0.0 : MIN2(a, currCep->GetMaxAccel(v, slope, isHBEV));
    }
    return a;
}


double
HelpersPHEMlight5::compute(const SUMOEmissionClass c, const PollutantsInterface::EmissionType e, const double v, const double a, const double slope, const EnergyParams* /* param */) const {
    const double corrSpeed = MAX2(0.0, v);
    double power = 0.;
    PHEMlightdllV5::CEP* currCep = myCEPs.count(c) == 0 ? 0 : myCEPs.find(c)->second;
    if (currCep != nullptr) {
        const double corrAcc = getModifiedAccel(c, corrSpeed, a, slope);
        const bool isHBEV = currCep->getFuelType() == PHEMlightdllV5::Constants::strBEV || currCep->getFuelType() == PHEMlightdllV5::Constants::strHybrid;
        if (isHBEV && corrAcc < currCep->GetDecelCoast(corrSpeed, corrAcc, slope) &&
                corrSpeed > PHEMlightdllV5::Constants::ZERO_SPEED_ACCURACY) {
            return 0;
        }
        power = currCep->CalcPower(corrSpeed, corrAcc, slope, isHBEV);
    }
    const std::string& fuelType = currCep->getFuelType();
    switch (e) {
        case PollutantsInterface::CO:
            return getEmission(currCep, "CO", power, corrSpeed) / SECONDS_PER_HOUR * 1000.;
        case PollutantsInterface::CO2:
            return currCep->GetCO2Emission(getEmission(currCep, "FC", power, corrSpeed),
                                           getEmission(currCep, "CO", power, corrSpeed),
                                           getEmission(currCep, "HC", power, corrSpeed), &myHelper) / SECONDS_PER_HOUR * 1000.;
        case PollutantsInterface::HC:
            return getEmission(currCep, "HC", power, corrSpeed) / SECONDS_PER_HOUR * 1000.;
        case PollutantsInterface::NO_X:
            return getEmission(currCep, "NOx", power, corrSpeed) / SECONDS_PER_HOUR * 1000.;
        case PollutantsInterface::PM_X:
            return getEmission(currCep, "PM", power, corrSpeed) / SECONDS_PER_HOUR * 1000.;
        case PollutantsInterface::FUEL: {
            if (fuelType == PHEMlightdllV5::Constants::strDiesel) { // divide by average diesel density of 836 g/l
                return getEmission(currCep, "FC", power, corrSpeed) / 836. / SECONDS_PER_HOUR * 1000.;
            } else if (fuelType == PHEMlightdllV5::Constants::strGasoline) { // divide by average gasoline density of 742 g/l
                return getEmission(currCep, "FC", power, corrSpeed) / 742. / SECONDS_PER_HOUR * 1000.;
            } else if (fuelType == PHEMlightdllV5::Constants::strBEV) {
                return 0;
            } else {
                return getEmission(currCep, "FC", power, corrSpeed) / SECONDS_PER_HOUR * 1000.; // surely false, but at least not additionally modified
            }
        }
        case PollutantsInterface::ELEC:
            if (fuelType == PHEMlightdllV5::Constants::strBEV) {
                return getEmission(currCep, "FC", power, corrSpeed) / SECONDS_PER_HOUR * 1000.;
            }
            return 0;
    }
    // should never get here
    return 0.;
}


/****************************************************************************/
