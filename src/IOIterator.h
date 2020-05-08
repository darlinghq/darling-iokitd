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

#ifndef IOKITD_IOITERATOR_H
#define IOKITD_IOITERATOR_H
#include "IOObject.h"
#include <vector>

class IOIterator : public IOObject
{
public:
	IOIterator(const std::vector<IOObject*>& objects);

	const char* className() const override;

	void reset();
	IOObject* next();
private:
	std::vector<IOObject*> m_objects;
	int m_pos = 0;
};

#endif

