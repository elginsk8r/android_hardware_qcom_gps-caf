/* Copyright (c) 2017-2021, The Linux Foundation. All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are
 * met:
 *     * Redistributions of source code must retain the above copyright
 *       notice, this list of conditions and the following disclaimer.
 *     * Redistributions in binary form must reproduce the above
 *       copyright notice, this list of conditions and the following
 *       disclaimer in the documentation and/or other materials provided
 *       with the distribution.
 *     * Neither the name of The Linux Foundation, nor the names of its
 *       contributors may be used to endorse or promote products derived
 *       from this software without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED "AS IS" AND ANY EXPRESS OR IMPLIED
 * WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES OF
 * MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND NON-INFRINGEMENT
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE COPYRIGHT OWNER OR CONTRIBUTORS
 * BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
 * CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
 * SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR
 * BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY,
 * WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE
 * OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN
 * IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 *
 */

#include "GnssAdapter.h"
#include "location_interface.h"

static GnssAdapter* gGnssAdapter = NULL;

typedef void (createOSFramework)();

static void initialize();
static void deinitialize();

static void addClient(LocationAPI* client, const LocationCallbacks& callbacks);
static void removeClient(LocationAPI* client, removeClientCompleteCallback rmClientCb);
static void requestCapabilities(LocationAPI* client);

static uint32_t startTracking(LocationAPI* client, TrackingOptions&);
static void updateTrackingOptions(LocationAPI* client, uint32_t id, TrackingOptions&);
static void stopTracking(LocationAPI* client, uint32_t id);

static void gnssNiResponse(LocationAPI* client, uint32_t id, GnssNiResponse response);
static uint32_t gnssDeleteAidingData(GnssAidingData& data);
static void gnssUpdateXtraThrottle(const bool enabled);

static void setControlCallbacks(LocationControlCallbacks& controlCallbacks);
static uint32_t enable(LocationTechnologyType techType);
static void disable(uint32_t id);
static uint32_t* gnssUpdateConfig(const GnssConfig& config);
static uint32_t* gnssGetConfig(GnssConfigFlagsMask mask);

static void gnssUpdateSvTypeConfig(GnssSvTypeConfig& config);
static void gnssGetSvTypeConfig(GnssSvTypeConfigCallback& callback);
static void gnssResetSvTypeConfig();

static void injectLocation(double latitude, double longitude, float accuracy);
static void injectLocationExt(const GnssLocationInfoNotification &locationInfo);
static void injectTime(int64_t time, int64_t timeReference, int32_t uncertainty);

static void agpsInit(const AgpsCbInfo& cbInfo);
static void agpsDataConnOpen(AGpsExtType agpsType, const char* apnName, int apnLen, int ipType);
static void agpsDataConnClosed(AGpsExtType agpsType);
static void agpsDataConnFailed(AGpsExtType agpsType);
static void getDebugReport(GnssDebugReport& report);
static void updateConnectionStatus(bool connected, int8_t type);
static void getGnssEnergyConsumed(GnssEnergyConsumedCallback energyConsumedCb);
static uint32_t setConstrainedTunc (bool enable, float tuncConstraint,
                                    uint32_t energyBudget);
static uint32_t setPositionAssistedClockEstimator(bool enable);

static void odcpiInit(const OdcpiRequestCallback& callback, OdcpiPrioritytype priority);
static void odcpiInject(const Location& location);

static void blockCPI(double latitude, double longitude, float accuracy,
                     int blockDurationMsec, double latLonDiffThreshold);
static void updateBatteryStatus(bool charging);
static void updatePowerState(PowerStateType powerState);
static uint32_t gnssUpdateSvConfig(const GnssSvTypeConfig& constellationEnablementConfig,
                                   const GnssSvIdConfig& blacklistSvConfig);
static uint32_t configLeverArm(const LeverArmConfigInfo& configInfo);
static uint32_t configRobustLocation(bool enable, bool enableForE911);
static uint32_t configMinGpsWeek(uint16_t minGpsWeek);
static uint32_t configDeadReckoningEngineParams(const DeadReckoningEngineConfig& dreConfig);
static uint32_t gnssUpdateSecondaryBandConfig(const GnssSvTypeConfig& secondaryBandConfig);
static uint32_t gnssGetSecondaryBandConfig();
static uint32_t configEngineRunState(PositioningEngineMask engType, LocEngineRunState engState);
static uint32_t configOutputNmeaTypes(GnssNmeaTypesMask enabledNmeaTypes);
static uint32_t setOptInStatus(bool userConsent);

static const GnssInterface gGnssInterface = {
    sizeof(GnssInterface),
    initialize,
    deinitialize,
    addClient,
    removeClient,
    requestCapabilities,
    startTracking,
    updateTrackingOptions,
    stopTracking,
    gnssNiResponse,
    setControlCallbacks,
    enable,
    disable,
    gnssUpdateConfig,
    gnssGetConfig,
    gnssUpdateSvTypeConfig,
    gnssGetSvTypeConfig,
    gnssResetSvTypeConfig,
    gnssDeleteAidingData,
    gnssUpdateXtraThrottle,
    injectLocation,
    injectTime,
    agpsInit,
    agpsDataConnOpen,
    agpsDataConnClosed,
    agpsDataConnFailed,
    getDebugReport,
    updateConnectionStatus,
    odcpiInit,
    odcpiInject,
    blockCPI,
    getGnssEnergyConsumed,
    injectLocationExt,
    updateBatteryStatus,
    updatePowerState,
    setConstrainedTunc,
    setPositionAssistedClockEstimator,
    gnssUpdateSvConfig,
    configLeverArm,
    configRobustLocation,
    configMinGpsWeek,
    configDeadReckoningEngineParams,
    gnssUpdateSecondaryBandConfig,
    gnssGetSecondaryBandConfig,
    configEngineRunState,
    configOutputNmeaTypes,
    setOptInStatus,
};

#ifndef DEBUG_X86
extern "C" const GnssInterface* getGnssInterface()
#else
const GnssInterface* getGnssInterface()
#endif // DEBUG_X86
{
   return &gGnssInterface;
}

static void createOSFrameworkInstance() {
    void* libHandle = nullptr;
    createOSFramework* getter = (createOSFramework*)dlGetSymFromLib(libHandle,
            "liblocationservice_glue.so", "createOSFramework");
    if (getter != nullptr) {
        (*getter)();
    } else {
        LOC_LOGe("dlGetSymFromLib failed for liblocationservice_glue.so");
    }
}

static void initialize()
{
    if (NULL == gGnssAdapter) {
        gGnssAdapter = new GnssAdapter();
        createOSFrameworkInstance();
    }
}

static void deinitialize()
{
    if (NULL != gGnssAdapter) {
        delete gGnssAdapter;
        gGnssAdapter = NULL;
    }
}

static void addClient(LocationAPI* client, const LocationCallbacks& callbacks)
{
    if (NULL != gGnssAdapter) {
        gGnssAdapter->addClientCommand(client, callbacks);
    }
}

static void removeClient(LocationAPI* client, removeClientCompleteCallback rmClientCb)
{
    if (NULL != gGnssAdapter) {
        gGnssAdapter->removeClientCommand(client, rmClientCb);
    }
}

static void requestCapabilities(LocationAPI* client)
{
    if (NULL != gGnssAdapter) {
        gGnssAdapter->requestCapabilitiesCommand(client);
    }
}

static uint32_t startTracking(
        LocationAPI* client, TrackingOptions& trackingOptions)
{
    if (NULL != gGnssAdapter) {
        return gGnssAdapter->startTrackingCommand(client, trackingOptions);
    } else {
        return 0;
    }
}

static void updateTrackingOptions(
        LocationAPI* client, uint32_t id, TrackingOptions& trackingOptions)
{
    if (NULL != gGnssAdapter) {
        gGnssAdapter->updateTrackingOptionsCommand(
                client, id, trackingOptions);
    }
}

static void stopTracking(LocationAPI* client, uint32_t id)
{
    if (NULL != gGnssAdapter) {
        gGnssAdapter->stopTrackingCommand(client, id);
    }
}

static void gnssNiResponse(LocationAPI* client, uint32_t id, GnssNiResponse response)
{
    if (NULL != gGnssAdapter) {
        gGnssAdapter->gnssNiResponseCommand(client, id, response);
    }
}

static void setControlCallbacks(LocationControlCallbacks& controlCallbacks)
{
    if (NULL != gGnssAdapter) {
        gGnssAdapter->setControlCallbacksCommand(controlCallbacks);
    }
}

static uint32_t enable(LocationTechnologyType techType)
{
    if (NULL != gGnssAdapter) {
        return gGnssAdapter->enableCommand(techType);
    } else {
        return 0;
    }
}

static void disable(uint32_t id)
{
    if (NULL != gGnssAdapter) {
        gGnssAdapter->disableCommand(id);
    }
}

static uint32_t* gnssUpdateConfig(const GnssConfig& config)
{
    if (NULL != gGnssAdapter) {
        return gGnssAdapter->gnssUpdateConfigCommand(config);
    } else {
        return NULL;
    }
}

static uint32_t* gnssGetConfig(GnssConfigFlagsMask mask)
{
    if (NULL != gGnssAdapter) {
        return gGnssAdapter->gnssGetConfigCommand(mask);
    } else {
        return NULL;
    }
}

static void gnssUpdateSvTypeConfig(GnssSvTypeConfig& config)
{
    if (NULL != gGnssAdapter) {
        gGnssAdapter->gnssUpdateSvTypeConfigCommand(config);
    }
}

static void gnssGetSvTypeConfig(GnssSvTypeConfigCallback& callback)
{
    if (NULL != gGnssAdapter) {
        gGnssAdapter->gnssGetSvTypeConfigCommand(callback);
    }
}

static void gnssResetSvTypeConfig()
{
    if (NULL != gGnssAdapter) {
        gGnssAdapter->gnssResetSvTypeConfigCommand();
    }
}

static uint32_t gnssDeleteAidingData(GnssAidingData& data)
{
    if (NULL != gGnssAdapter) {
        return gGnssAdapter->gnssDeleteAidingDataCommand(data);
    } else {
        return 0;
    }
}

static void gnssUpdateXtraThrottle(const bool enabled)
{
    if (NULL != gGnssAdapter) {
        gGnssAdapter->gnssUpdateXtraThrottleCommand(enabled);
    }
}

static void injectLocation(double latitude, double longitude, float accuracy)
{
   if (NULL != gGnssAdapter) {
       gGnssAdapter->injectLocationCommand(latitude, longitude, accuracy);
   }
}

static void injectTime(int64_t time, int64_t timeReference, int32_t uncertainty)
{
   if (NULL != gGnssAdapter) {
       gGnssAdapter->injectTimeCommand(time, timeReference, uncertainty);
   }
}

static void agpsInit(const AgpsCbInfo& cbInfo) {

    if (NULL != gGnssAdapter) {
        gGnssAdapter->initAgpsCommand(cbInfo);
    }
}
static void agpsDataConnOpen(
        AGpsExtType agpsType, const char* apnName, int apnLen, int ipType) {

    if (NULL != gGnssAdapter) {
        gGnssAdapter->dataConnOpenCommand(
                agpsType, apnName, apnLen, (AGpsBearerType)ipType);
    }
}
static void agpsDataConnClosed(AGpsExtType agpsType) {

    if (NULL != gGnssAdapter) {
        gGnssAdapter->dataConnClosedCommand(agpsType);
    }
}
static void agpsDataConnFailed(AGpsExtType agpsType) {

    if (NULL != gGnssAdapter) {
        gGnssAdapter->dataConnFailedCommand(agpsType);
    }
}

static void getDebugReport(GnssDebugReport& report) {

    if (NULL != gGnssAdapter) {
        gGnssAdapter->getDebugReport(report);
    }
}

static void updateConnectionStatus(bool connected, int8_t type) {
    if (NULL != gGnssAdapter) {
        gGnssAdapter->getSystemStatus()->eventConnectionStatus(connected, type);
    }
}

static void odcpiInit(const OdcpiRequestCallback& callback, OdcpiPrioritytype priority)
{
    if (NULL != gGnssAdapter) {
        gGnssAdapter->initOdcpiCommand(callback, priority);
    }
}

static void odcpiInject(const Location& location)
{
    if (NULL != gGnssAdapter) {
        gGnssAdapter->injectOdcpiCommand(location);
    }
}

static void blockCPI(double latitude, double longitude, float accuracy,
                     int blockDurationMsec, double latLonDiffThreshold) {
    if (NULL != gGnssAdapter) {
        gGnssAdapter->blockCPICommand(latitude, longitude, accuracy,
                                      blockDurationMsec, latLonDiffThreshold);
    }
}

static void getGnssEnergyConsumed(GnssEnergyConsumedCallback energyConsumedCb) {
    if (NULL != gGnssAdapter) {
        gGnssAdapter->getGnssEnergyConsumedCommand(energyConsumedCb);
    }
}

static void injectLocationExt(const GnssLocationInfoNotification &locationInfo)
{
   if (NULL != gGnssAdapter) {
       gGnssAdapter->injectLocationExtCommand(locationInfo);
   }
}

static void updateBatteryStatus(bool charging) {
    if (NULL != gGnssAdapter) {
        gGnssAdapter->getSystemStatus()->updatePowerConnectState(charging);
    }
}

static void updatePowerState(PowerStateType powerEvent) {
   if (NULL != gGnssAdapter) {
       gGnssAdapter->updatePowerStateCommand(powerEvent);
   }
}

static uint32_t setConstrainedTunc (bool enable, float tuncConstraint, uint32_t energyBudget) {
    if (NULL != gGnssAdapter) {
        return gGnssAdapter->setConstrainedTuncCommand(enable, tuncConstraint, energyBudget);
    } else {
        return 0;
    }
}

static uint32_t setPositionAssistedClockEstimator(bool enable) {
    if (NULL != gGnssAdapter) {
        return gGnssAdapter->setPositionAssistedClockEstimatorCommand(enable);
    } else {
        return 0;
    }
}

static uint32_t gnssUpdateSvConfig(
        const GnssSvTypeConfig& constellationEnablementConfig,
        const GnssSvIdConfig&   blacklistSvConfig) {
    if (NULL != gGnssAdapter) {
        return gGnssAdapter->gnssUpdateSvConfigCommand(
                constellationEnablementConfig, blacklistSvConfig);
    } else {
        return 0;
    }
}

static uint32_t configLeverArm(const LeverArmConfigInfo& configInfo){
    if (NULL != gGnssAdapter) {
        return gGnssAdapter->configLeverArmCommand(configInfo);
    } else {
        return 0;
    }
}

static uint32_t configRobustLocation(bool enable, bool enableForE911){
    if (NULL != gGnssAdapter) {
        return gGnssAdapter->configRobustLocationCommand(enable, enableForE911);
    } else {
        return 0;
    }
}

static uint32_t configMinGpsWeek(uint16_t minGpsWeek){
    if (NULL != gGnssAdapter) {
        return gGnssAdapter->configMinGpsWeekCommand(minGpsWeek);
    } else {
        return 0;
    }
}

static uint32_t configDeadReckoningEngineParams(const DeadReckoningEngineConfig& dreConfig){
    if (NULL != gGnssAdapter) {
        return gGnssAdapter->configDeadReckoningEngineParamsCommand(dreConfig);
    } else {
        return 0;
    }
}

static uint32_t gnssUpdateSecondaryBandConfig(
        const GnssSvTypeConfig& secondaryBandConfig) {
    if (NULL != gGnssAdapter) {
        return gGnssAdapter->gnssUpdateSecondaryBandConfigCommand(secondaryBandConfig);
    } else {
        return 0;
    }
}

static uint32_t gnssGetSecondaryBandConfig(){
    if (NULL != gGnssAdapter) {
        return gGnssAdapter->gnssGetSecondaryBandConfigCommand();
    } else {
        return 0;
    }
}

static uint32_t configEngineRunState(PositioningEngineMask engType, LocEngineRunState engState) {
    if (NULL != gGnssAdapter) {
        return gGnssAdapter->configEngineRunStateCommand(engType, engState);
    } else {
        return 0;
    }
}

static uint32_t configOutputNmeaTypes (GnssNmeaTypesMask enabledNmeaTypes) {
    if (NULL != gGnssAdapter) {
        return gGnssAdapter->configOutputNmeaTypesCommand(enabledNmeaTypes);
    } else {
        return 0;
    }
}

static uint32_t setOptInStatus(bool userConsent) {
    if (NULL != gGnssAdapter) {
        struct RespMsg : public LocMsg {
            uint32_t mSessionId;
            inline RespMsg(uint32_t id) : LocMsg(), mSessionId(id) {}
            inline void proc() const override {
                gGnssAdapter->reportResponse(LOCATION_ERROR_SUCCESS, mSessionId);
            }
        };

        uint32_t sessionId = gGnssAdapter->generateSessionId();
        gGnssAdapter->getSystemStatus()->eventOptInStatus(userConsent);
        gGnssAdapter->sendMsg(new RespMsg(sessionId));

        return sessionId;
    } else {
        return 0;
    }
}
