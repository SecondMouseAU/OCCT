// Created on: 1997-03-06
// Created by: Mister rmi
// Copyright (c) 1997-1999 Matra Datavision
// Copyright (c) 1999-2014 OPEN CASCADE SAS
//
// This file is part of Open CASCADE Technology software library.
//
// This library is free software; you can redistribute it and/or modify it under
// the terms of the GNU Lesser General Public License version 2.1 as published
// by the Free Software Foundation, with special exception defined in the file
// OCCT_LGPL_EXCEPTION.txt. Consult the file LICENSE_LGPL_21.txt included in OCCT
// distribution for complete text of the license and disclaimer of any warranty.
//
// Alternatively, this file may be used under the terms of Open CASCADE
// commercial license or contractual agreement.

#include <OSD_SharedLibrary.hxx>
#include <Plugin.hxx>
#include <Plugin_Failure.hxx>
#include <OSD_Function.hxx>
#include <TCollection_AsciiString.hxx>
#include <NCollection_DataMap.hxx>
#include <Resource_Manager.hxx>
#include <Standard_GUID.hxx>
#include <Standard_Transient.hxx>

#include <Standard_WarningDisableFunctionCast.hxx>

#include <shared_mutex>
#include <mutex>
#ifdef __wasi__
// WASI noeh libc++ doesn't have shared_mutex - use spinlock
#include <atomic>
#endif

//=================================================================================================

occ::handle<Standard_Transient> Plugin::Load(const Standard_GUID& aGUID, const bool theVerbose)
{
  char                aPluginIdBuf[1000];
  Standard_PCharacter aPluginId = aPluginIdBuf;
  aGUID.ToCString(aPluginId);
  TCollection_AsciiString pid(aPluginId);

#ifndef __wasi__
  static std::shared_mutex                                          aMapMutex;
#else
  // WASI noeh libc++ doesn't have shared_mutex - use spinlock
  static std::atomic_flag       aMapSpinlock = ATOMIC_FLAG_INIT;
  static int                    aReadersCount = 0;
  static std::atomic_flag       aWriterLock = ATOMIC_FLAG_INIT;
#endif
  static NCollection_DataMap<TCollection_AsciiString, OSD_Function> theMapOfFunctions;
  OSD_Function                                                      f;

  // Fast path: read-only cache lookup under shared lock.
  {
#ifndef __wasi__
    std::shared_lock<std::shared_mutex> aReadLock(aMapMutex);
#else
    // WASI: spinlock-based read lock
    while (aWriterLock.test_and_set(std::memory_order_acquire)) {}
    aReadersCount++;
    aWriterLock.clear(std::memory_order_release);
#endif
    if (theMapOfFunctions.Find(pid, f))
    {
#ifndef __wasi__
      aReadLock.unlock();
#else
      aReadersCount--;
#endif;
      Standard_Transient* (*fp)(const Standard_GUID&) =
        reinterpret_cast<Standard_Transient* (*)(const Standard_GUID&)>(reinterpret_cast<void*>(f));
      return (*fp)(aGUID);
    }
  }

  // Slow path: exclusive lock for plugin loading.
#ifndef __wasi__
  std::unique_lock<std::shared_mutex> aWriteLock(aMapMutex);
#else
  // WASI: spinlock-based write lock
  while (aWriterLock.test_and_set(std::memory_order_acquire)) {}
  while (aReadersCount > 0) {
    // wait for readers to finish
  }
#endif
  if (!theMapOfFunctions.IsBound(pid))
  {
    occ::handle<Resource_Manager> PluginResource = new Resource_Manager("Plugin");
    TCollection_AsciiString       theResource(aPluginId);
    theResource += ".Location";

    if (!PluginResource->Find(theResource.ToCString()))
    {
      Standard_SStream aMsg;
      aMsg << "could not find the resource:";
      aMsg << theResource.ToCString() << '\n';
      if (theVerbose)
      {
        std::cout << "could not find the resource:" << theResource.ToCString() << '\n';
      }
      throw Plugin_Failure(aMsg.str().c_str());
    }

    TCollection_AsciiString thePluginLibrary("");
#ifndef _WIN32
    thePluginLibrary += "lib";
#endif
    thePluginLibrary += PluginResource->Value(theResource.ToCString());
#ifdef _WIN32
    thePluginLibrary += ".dll";
#elif defined(__APPLE__)
    thePluginLibrary += ".dylib";
#elif defined(HPUX) || defined(_hpux)
    thePluginLibrary += ".sl";
#else
    thePluginLibrary += ".so";
#endif
    OSD_SharedLibrary theSharedLibrary(thePluginLibrary.ToCString());
    if (!theSharedLibrary.DlOpen(OSD_RTLD_LAZY))
    {
      TCollection_AsciiString error(theSharedLibrary.DlError());
      Standard_SStream        aMsg;
      aMsg << "could not open:";
      aMsg << PluginResource->Value(theResource.ToCString());
      aMsg << "; reason:";
      aMsg << error.ToCString();
      if (theVerbose)
      {
        std::cout << "could not open: " << PluginResource->Value(theResource.ToCString())
                  << " ; reason: " << error.ToCString() << '\n';
      }
      throw Plugin_Failure(aMsg.str().c_str());
    }
    f = theSharedLibrary.DlSymb("PLUGINFACTORY");
    if (f == nullptr)
    {
      TCollection_AsciiString error(theSharedLibrary.DlError());
      Standard_SStream        aMsg;
      aMsg << "could not find the factory in:";
      aMsg << PluginResource->Value(theResource.ToCString());
      aMsg << error.ToCString();
      throw Plugin_Failure(aMsg.str().c_str());
    }
    theMapOfFunctions.Bind(pid, f);
  }
  else
  {
    f = theMapOfFunctions(pid);
  }
#ifndef __wasi__
  aWriteLock.unlock();
#else
  aWriterLock.clear(std::memory_order_release);
#endif

  // Cast through void* to avoid -Wcast-function-type-mismatch warning.
  // This is safe for dynamically loaded plugin symbols.
  Standard_Transient* (*fp)(const Standard_GUID&) =
    reinterpret_cast<Standard_Transient* (*)(const Standard_GUID&)>(reinterpret_cast<void*>(f));
  return (*fp)(aGUID);
}
