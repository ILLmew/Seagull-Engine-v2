#include "StdAfx.h"
#include "SystemManager.h"

#include "Core/Memory/Memory.h"

#include "Core/Log/Log.h"
#include "Core/FileSystem/FileSystem.h"
#include "Core/Platform/OperatingSystem.h"

namespace SG
{
	// no implementation yet, just forward declaration
	class C2DEngine;

	CSystemManager* CSystemManager::sInstance = nullptr;

	CSystemManager::CSystemManager()
		: mpCurrActiveProcess(nullptr), mRootPath("")
	{}

	void CSystemManager::InitSystemEnv()
	{
		char abPath[SG_MAX_FILE_PATH] = { 0 };
		GetModuleFileNameA(NULL, abPath, sizeof(abPath));
		char drivePath[SG_MAX_DRIVE_PATH] = { 0 };
		char directoryPath[SG_MAX_DIREC_PATH] = { 0 };
		_splitpath_s(abPath,
			drivePath, SG_MAX_DRIVE_PATH,
			directoryPath, SG_MAX_DIREC_PATH, NULL, 0, NULL, 0);

		string rootPath(drivePath);
		rootPath.append(directoryPath);
		mRootPath = move(rootPath);
		// set root directory to where the .exe file is
		_chdir(mRootPath.c_str());
	}

	void CSystemManager::SetI3DEngine(I3DEngine* p3DEngine) { mSystemModules.p3DEngine = p3DEngine; }
	void CSystemManager::SetI2DEngine(I2DEngine* p2DEngine) { mSystemModules.p2DEngine = p2DEngine; }

	SG::ISystemModules* CSystemManager::GetSystemModules() { return &mSystemModules; }
	SG::I3DEngine*      CSystemManager::GetI3DEngine() { return mSystemModules.p3DEngine; }
	SG::I2DEngine*      CSystemManager::GetI2DEngine() { return mSystemModules.p2DEngine; }
	SG::ILog*           CSystemManager::GetILog() { return mSystemModules.pLog; }
	SG::IFileSystem*    CSystemManager::GetIFileSystem() { return mSystemModules.pFileSystem; }
	SG::IOperatingSystem* CSystemManager::GetIOS() { return mSystemModules.pOS; }

	bool CSystemManager::ValidateCoreModules() const
	{
		bool isReady = (mSystemModules.pLog != nullptr) &&
			(mSystemModules.pFileSystem != nullptr) &&
			(mSystemModules.pOS != nullptr);
		return isReady;
	}

	bool CSystemManager::ValidateAllModules() const
	{
		bool isReady = ValidateCoreModules() &&
			(mSystemModules.p2DEngine != nullptr) &&
			(mSystemModules.p3DEngine != nullptr);
		return isReady;
	}

	void CSystemManager::AddIProcess(IProcess* pProcess)
	{
		mpCurrActiveProcess = pProcess;
		mpCurrActiveProcess->OnInit();
	}

	SG::UInt32 CSystemManager::GetTotalMemoryUsage() const
	{
		// no implementation yet.
		return 0;
	}

	bool CSystemManager::InitCoreModules()
	{
		if (!mSystemModules.pFileSystem) mSystemModules.pFileSystem = New<CFileSystem>();
		if (!mSystemModules.pLog)        mSystemModules.pLog = New<CLog>();
		if (!mSystemModules.pOS)         mSystemModules.pOS = New<COperatingSystem>();
		mSystemModules.pFileSystem->OnInit();
		mSystemModules.pLog->OnInit();
		mSystemModules.pOS->OnInit();
		return ValidateCoreModules();
	}

	void CSystemManager::Update()
	{
		if (mSystemModules.p3DEngine) mSystemModules.p3DEngine->OnUpdate();
		if (mSystemModules.p2DEngine) mSystemModules.p2DEngine->OnUpdate();
		if (mpCurrActiveProcess) mpCurrActiveProcess->OnUpdate();
	}

	void CSystemManager::Shutdown()
	{
		if (mpCurrActiveProcess) mpCurrActiveProcess->OnShutdown();

		mSystemModules.pOS->OnShutdown();
		mSystemModules.pLog->OnShutdown();
		Delete(mSystemModules.pOS);
		Delete(mSystemModules.pLog);
		Delete(mSystemModules.pFileSystem);
		mSystemModules.pOS = nullptr;
		mSystemModules.pFileSystem = nullptr;
		mSystemModules.pLog = nullptr;

		if (sInstance)
			Delete(sInstance);
	}

	ISystemManager* CSystemManager::GetInstance()
	{
		if (sInstance == nullptr)
			sInstance = New<CSystemManager>();
		return sInstance;
	}

	bool CSystemManager::SystemMainLoop()
	{
		bool bIsSafeQuit = true;
		bool bIsExit = false;
		while (!bIsExit)
		{
			EOsMessage msg = EOsMessage::eNull;
			msg = PeekOSMessage();
			if (msg == EOsMessage::eQuit)
				bIsExit = true;

			// TODO: remove the Update() interface.
			Update();
		}
		return bIsSafeQuit;
	}

}