/*
 This file is part of Darling.

 Copyright (C) 2020 Lubos Dolezel

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
#include "IOSurface.h"
#include "IOSurfaceRoot.h"
#include <IOSurface/IOSurfaceRef.h>
#include <Foundation/NSNumber.h>
#include <CoreVideo/CVPixelBuffer.h>
#include <stdexcept>

IOSurface::IOSurface(IOSurfaceID myId, NSDictionary* properties)
: m_id(myId)
{
	// kIOSurfaceWidth
	// kIOSurfaceHeight
	// kIOSurfaceBytesPerElement
	// kIOSurfacePixelFormat
	// kIOSurfaceBytesPerRow
	// kIOSurfaceIsGlobal

	int width = -1, height = -1;
	if (id numWidth = properties[(NSString*)kIOSurfaceWidth])
	{
		if ([numWidth isKindOfClass: [NSNumber class]])
			width = [numWidth intValue];
	}
	if (id numHeight = properties[(NSString*)kIOSurfaceHeight])
	{
		if ([numHeight isKindOfClass: [NSNumber class]])
			height = [numHeight intValue];
	}

	if (width <= 0 || height <= 0)
		throw std::runtime_error("Missing or invalid kIOSurfaceWidth/kIOSurfaceHeight");

	uint32_t pixelFormat = 0;
	if (id numPixelFormat = properties[(NSString*)kIOSurfacePixelFormat])
	{
		if ([numPixelFormat isKindOfClass: [NSNumber class]])
			pixelFormat = [numPixelFormat unsignedIntegerValue];
	}

	if (pixelFormat == 0)
		throw std::runtime_error("Missing kIOSurfacePixelFormat");

#if 0
	// Determine a good storage format.
	// Note that the storage format doesn't need to precisely match what ends up being stored,
	// only the bit count must match.
	// TODO: YUV definitely needs additional attention once CoreVideo is implemented.
	GLenum internalFormat = GL_RGBA8UI;
	switch (pixelFormat)
	{
		case kCVPixelFormatType_422YpCbCr8FullRange:
		case kCVPixelFormatType_422YpCbCr8:
		case kCVPixelFormatType_422YpCbCr16:
		case kCVPixelFormatType_422YpCbCr10:
		case kCVPixelFormatType_422YpCbCr8_yuvs:
		case kCVPixelFormatType_422YpCbCr8FullRange:
		case kCVPixelFormatType_16BE555:
		case kCVPixelFormatType_16LE555:
		case kCVPixelFormatType_16LE5551:
		case kCVPixelFormatType_16BE565:
		case kCVPixelFormatType_16LE565:
		case kCVPixelFormatType_16Gray:
			internalFormat = GL_RG8; // 16 bits
			break;
		case kCVPixelFormatType_444YpCbCr10BiPlanarVideoRange:
		case kCVPixelFormatType_444YpCbCr10BiPlanarFullRange:
		case kCVPixelFormatType_24RGB:
		case kCVPixelFormatType_24BGR:
			internalFormat = GL_RGB8UI; // 24 bits
			break;
		case kCVPixelFormatType_32BGRA:
		case kCVPixelFormatType_32ABGR:
		case kCVPixelFormatType_32RGBA:
			internalFormat = GL_RGBA8UI; // 32 bits
			break;
	}

	if (properties[kIOSurfaceIsGlobal] == kCFBooleanTrue)
		m_global = true;
	
	unsigned int texture;
	glGenTextures(1, &texture);
	glBindTexture(GL_TEXTURE_2D, texture);
	glTexStorage2D(GL_TEXTURE_2D, 1, internalFormat, width, height);
#endif
}

IOSurface::~IOSurface()
{
	IOSurfaceRoot::instance()->surfaceDestroyed(m_id);
}

const char* IOSurface::className() const
{
	return "IOSurface";
}

void IOSurface::incrementUseCount()
{
	m_useCount++;
}

void IOSurface::decrementUseCount()
{
	m_useCount--;
}

void IOSurface::lock()
{

}

void IOSurface::unlock()
{

}
