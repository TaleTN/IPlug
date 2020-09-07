#pragma once

enum EHost
{
	kHostUninit = -1,
	kHostUnknown = 0,
	kHostReaper,
	kHostProTools,
	kHostCubase,
	kHostNuendo,
	kHostSonar,
	kHostVegas,
	kHostFL,
	kHostSamplitude,
	kHostAbletonLive,
	kHostTracktion,
	kHostNTracks,
	kHostMelodyneStudio,
	kHostVSTScanner,
	kHostAULab,
	kHostForte,
	kHostChainer,
	kHostAudition,
	kHostOrion,
	kHostBias,
	kHostSAWStudio,
	kHostLogic,
	kHostDigitalPerformer,
	kHostAudioMulch,
	kHostStudioOne,
	kHostMixcraft,
	kHostGarageBand,
	kHostArdour,

	// These hosts don't report the host name:
	// EnergyXT2
	// MiniHost
};

int LookUpHost(const char* host);
