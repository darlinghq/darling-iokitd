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
#ifndef IOOBJECTREF_H
#define IOOBJECTREF_H

template <typename IOObjectType>
class IOObjectRef
{
public:
	IOObjectRef(IOObjectType* obj)
	: m_obj(obj)
	{
		if (m_obj)
			m_obj->retain();
	}
	IOObjectRef(const IOObjectRef<IOObjectType>& that)
	{
		m_obj = that.m_obj;
		if (m_obj)
			m_obj->retain();
	}
	IOObjectRef(const IOObjectRef<IOObjectType>&& that)
	{
		m_obj = that.m_obj;
		that.m_obj = nullptr;
	}
	~IOObjectRef()
	{
		if (m_obj != nullptr)
			m_obj->release();
	}
	operator bool()
	{
		return m_obj != nullptr;
	}
	IOObjectType* operator->()
	{
		return m_obj;
	}
	operator IOObjectType*()
	{
		return m_obj;
	}

	template <typename NewType>
	IOObjectRef<NewType> cast()
	{
		NewType* t = dynamic_cast<NewType>(m_obj);
		return IOObjectRef<NewType>(t);
	}
private:
	IOObjectType* m_obj;
};

#endif
