#pragma once

#include "Common/Base/BasicTypes.h"

#include <stdio.h>

namespace SG
{

#ifdef __cplusplus
extern "C"
{
#endif

	//! Resource directory base on the root directory.
	typedef enum class EResourceDirectory
	{
		eRoot = 0,
		eShader_Binarires,
		eShader_Sources,
		eMeshes,
		eTextures,
		eFonts,
		eLog,
		eScripts,
		Num_Directory,
	} EResourceDirectory;

	//! Resourece filters to define the usage of files.
	typedef enum class EResoureceFilter
	{
		eGame_Resoureces = 0, //! Resources used in game
		eGame_Savedatas,      //! Savedatas for game
		eGame_Scripts,        //! Scripts used in game
		eDebug,               //! During development, for debug purpose
	} EResoureceFilter;

	typedef enum class EFileBaseOffset
	{
		eStart,
		eCurrent,
		eEOF,
	} EFileBaseOffset;

	typedef enum class EFileMode
	{
		eRead = 0x01,
		eWrite = 0x02,
		eAppend = 0x04,
		eBinary = 0x08,

		eRead_Write = eRead | eWrite,
		eRead_Binary = eRead | eBinary,
		eWrite_Binary = eWrite | eBinary,
		eAppend_Binary = eAppend | eBinary,
		eRead_Write_Binary = eRead_Write | eBinary,
		eUndefined = 0xff,
	} EFileMode;
	SG_ENUM_CLASS_FLAG(UInt32, EFileMode);

	//! Memory stream for binary
	typedef struct Memory
	{
		UInt8* pBuffer;
		Size   cursor;
	} Memory;

	typedef struct FileStream
	{
		union
		{
			Memory memory;
			FILE*  file;
		};
		EFileMode  filemode;
	} FileStream;

	//! Stream operations to manipulate the files in the disk
	typedef struct IStreamOp
	{
		virtual ~IStreamOp() = default;

		virtual bool Open(const EResourceDirectory directory, const char* filename, const EFileMode filemode, FileStream* pOut) = 0;
		virtual bool Close(FileStream* pStream) = 0;
		virtual Size Read(FileStream* pStream, void* pInBuf, Size bufSize) = 0;
		virtual Size Write(FileStream* pStream, const void* const pOutBuf, Size bufSize) = 0;
		virtual bool Seek(const FileStream* pStream, EFileBaseOffset baseOffset, Size offset) const = 0;
		virtual Size Tell(const FileStream* pStream) const = 0;
		virtual Size FileSize(const FileStream* pStream) const = 0;
		virtual bool Flush(FileStream* pStream) = 0;
		virtual bool IsEndOfFile(const FileStream* pStream) const = 0;

		virtual const char* GetRootDirectory() const = 0;
		virtual void        SetRootDirectory(const char* rootFolder) = 0;
	} IStreamOp;

	// TODO: add async file request system to do async file io (after the thread system) 
	//! @Interface
	//! File system to do io relative job
	typedef struct IFileSystem
	{
		virtual ~IFileSystem() = default;

		//! Initialize file system
		virtual void OnInit() = 0;
		//! Shutdown file system
		virtual void OnShutdown() = 0;

		//! User interface to set the stream op to user custom.
		virtual void SetIStreamOp(IStreamOp* pStreamOp) = 0;

		//! Set engine's resource files root directory.
		//! Default root directory will be the folder where .exe is in.
		//! \param (filepath) relative path to the .exe
		virtual void SetRootDirectory(const char* filepath) = 0;

		virtual bool Open(const EResourceDirectory directory, const char* filename, const EFileMode filemode) = 0;
		virtual bool Close() = 0;
		virtual Size Read(void* pInBuf, Size bufSize) = 0;
		virtual Size Write(const void* const pOutBuf, Size bufSize) = 0;
		virtual bool Seek(EFileBaseOffset baseOffset, Size offset) = 0;
		virtual Size Tell() const = 0;
		virtual Size FileSize() const = 0;
		virtual bool Flush() = 0;
		virtual bool IsEndOfFile() const = 0;
	} IFileSystem;

#ifdef __cplusplus
} // end extern C
#endif

}