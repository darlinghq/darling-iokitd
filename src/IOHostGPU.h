/*
 This file is part of Darling.

 Copyright (C) 2026 Lubos Dolezel

 Darling is free software: you can redistribute it and/or modify
 it under the terms of the GNU General Public License as published by
 the Free Software Foundation, either version 3 of the License, or
 (at your option) any later version.

 Darling is distributed in the hope that it will be useful,
 but WITHOUT ANY WARRANTY; without even the implied warranty of
 MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 GNU General Public License for more details.

 You should have received a copy of the GNU General Public License
 along with Darling.  If not, see <http://www.gnu.org/licenses/>.
*/

#ifndef IOKITD_IOHOSTGPU_H
#define IOKITD_IOHOSTGPU_H

#include "IOService.h"
#include "ServiceRegistry.h"

// The GPUs of the host, as the service applications (and OpenGL.framework) look for to find
// out how much graphics memory there is: IOServiceMatching("IOAccelerator"), one per DRM device
// the host has. It carries the PCI vendor-id and device-id and "VRAM,totalMB" (megabytes), the
// memory the GPU has of its own, which the DRM driver tells (amdgpu does).
//
// Policy of Darling (not of macOS): for a GPU whose driver tells nothing (integrated and virtual
// GPUs, which use system memory) the figure is half of the system memory, so that applications
// that size their graphics budget from it do not assume a tiny GPU. This is the only place that
// decides it.
//
// No IOFramebuffer is published: Apple silicon Macs have none, so applications handle its absence.
class IOHostGPU : public IOService
{
protected:
	IOHostGPU(NSDictionary* properties);
public:
	NSDictionary* getProperties() override;

	// Creates the services for the DRM devices found in sysfs_drm_directory
	// (the host's /sys is visible below /Volumes/SystemRoot)
	static void registerSelf(ServiceRegistry* targetServiceRegistry, const char* sysfs_drm_directory = "/Volumes/SystemRoot/sys/class/drm");
protected:
	NSDictionary* m_properties;
};

class IOAccelerator : public IOHostGPU
{
public:
	IOAccelerator(NSDictionary* properties) : IOHostGPU(properties) {}
	const char* className() const override;
	NSDictionary* matchingDictionary() override;
};

#endif
