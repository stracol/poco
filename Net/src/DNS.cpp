//
// DNS.cpp
//
// $Id: //poco/1.3/Net/src/DNS.cpp#2 $
//
// Library: Net
// Package: NetCore
// Module:  DNS
//
// Copyright (c) 2005-2006, Applied Informatics Software Engineering GmbH.
// and Contributors.
//
// Permission is hereby granted, free of charge, to any person or organization
// obtaining a copy of the software and accompanying documentation covered by
// this license (the "Software") to use, reproduce, display, distribute,
// execute, and transmit the Software, and to prepare derivative works of the
// Software, and to permit third-parties to whom the Software is furnished to
// do so, all subject to the following:
// 
// The copyright notices in the Software and this entire statement, including
// the above license grant, this restriction and the following disclaimer,
// must be included in all copies of the Software, in whole or in part, and
// all derivative works of the Software, unless such copies or derivative
// works are solely in the form of machine-executable object code generated by
// a source language processor.
// 
// THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
// IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
// FITNESS FOR A PARTICULAR PURPOSE, TITLE AND NON-INFRINGEMENT. IN NO EVENT
// SHALL THE COPYRIGHT HOLDERS OR ANYONE DISTRIBUTING THE SOFTWARE BE LIABLE
// FOR ANY DAMAGES OR OTHER LIABILITY, WHETHER IN CONTRACT, TORT OR OTHERWISE,
// ARISING FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER
// DEALINGS IN THE SOFTWARE.
//


#include "Poco/Net/DNS.h"
#include "Poco/Net/NetException.h"
#include "Poco/Net/SocketAddress.h"
#include "Poco/Environment.h"
#include "Poco/NumberFormatter.h"


using Poco::FastMutex;
using Poco::Environment;
using Poco::NumberFormatter;
using Poco::IOException;


//
// Automatic initialization of Windows networking
//
#if defined(_WIN32)
namespace
{
	class NetworkInitializer
	{
	public:
		NetworkInitializer()
		{
			WORD    version = MAKEWORD(2, 2);
			WSADATA data;
			WSAStartup(version, &data);
		}
		
		~NetworkInitializer()
		{
			WSACleanup();
		}
	};
	
	static NetworkInitializer networkInitializer;
}
#endif // _WIN32


namespace Poco {
namespace Net {


DNS::DNSCache DNS::_cache;
Poco::FastMutex DNS::_mutex;


const HostEntry& DNS::hostByName(const std::string& hostname)
{
	FastMutex::ScopedLock lock(_mutex);
	
	DNSCache::const_iterator it = _cache.find(hostname);
	if (it != _cache.end())
	{
		return it->second;
	}
	else
	{
#if defined(_WIN32) && defined(POCO_HAVE_IPv6)
		struct addrinfo* pAI;
		struct addrinfo hints;
		memset(&hints, 0, sizeof(hints));
		hints.ai_flags = AI_CANONNAME;
		if (getaddrinfo(hostname.c_str(), NULL, &hints, &pAI) == 0)
		{
			std::pair<DNSCache::iterator, bool> res = _cache.insert(std::pair<std::string, HostEntry>(hostname, HostEntry(pAI)));
			freeaddrinfo(pAI);
			return res.first->second;
		}
#else
		struct hostent* he = gethostbyname(hostname.c_str());
		if (he)
		{
			std::pair<DNSCache::iterator, bool> res = _cache.insert(std::pair<std::string, HostEntry>(hostname, HostEntry(he)));
			return res.first->second;
		}
#endif
	}
	error(lastError(), hostname);      // will throw an appropriate exception
	throw NetException(); // to silence compiler
}


const HostEntry& DNS::hostByAddress(const IPAddress& address)
{
	FastMutex::ScopedLock lock(_mutex);

#if defined(_WIN32) && defined(POCO_HAVE_IPv6)
	SocketAddress sa(address, 0);
	static char fqname[1024];
	if (getnameinfo(sa.addr(), sa.length(), fqname, sizeof(fqname), NULL, 0, 0) == 0)
	{
		DNSCache::const_iterator it = _cache.find(std::string(fqname));
		if (it != _cache.end())
		{
			return it->second;
		}
		else
		{
			struct addrinfo* pAI;
			struct addrinfo hints;
			memset(&hints, 0, sizeof(hints));
			if (getaddrinfo(fqname, NULL, &hints, &pAI) == 0)
			{
				std::pair<DNSCache::iterator, bool> res = _cache.insert(std::pair<std::string, HostEntry>(std::string(fqname), HostEntry(pAI)));
				freeaddrinfo(pAI);
				return res.first->second;
			}
		}
	}
#else
	struct hostent* he = gethostbyaddr(reinterpret_cast<const char*>(address.addr()), address.length(), address.af());
	if (he)
	{
		std::pair<DNSCache::iterator, bool> res = _cache.insert(std::pair<std::string, HostEntry>(std::string(he->h_name), HostEntry(he)));
		return res.first->second;
	}
#endif
	error(lastError(), address.toString());      // will throw an appropriate exception
	throw NetException(); // to silence compiler
}


const HostEntry& DNS::resolve(const std::string& address)
{
	IPAddress ip;
	if (IPAddress::tryParse(address, ip))
		return hostByAddress(ip);
	else
		return hostByName(address);
}


IPAddress DNS::resolveOne(const std::string& address)
{
	const HostEntry& entry = resolve(address);
	if (!entry.addresses().empty())
		return entry.addresses()[0];
	else
		throw NoAddressFoundException(address);
}


const HostEntry& DNS::thisHost()
{
	return hostByName(hostName());
}


void DNS::flushCache()
{
	FastMutex::ScopedLock lock(_mutex);

	_cache.clear();
}


std::string DNS::hostName()
{
	char buffer[256];
	int rc = gethostname(buffer, sizeof(buffer));
	if (rc == 0)
		return std::string(buffer);
	else
		throw NetException("Cannot get host name");
}


int DNS::lastError()
{
#if defined(_WIN32)
	return GetLastError();
#else
	return h_errno;
#endif
}

	
void DNS::error(int code, const std::string& arg)
{
	switch (code)
	{
	case POCO_ESYSNOTREADY:
		throw NetException("Net subsystem not ready");
	case POCO_ENOTINIT:
		throw NetException("Net subsystem not initialized");
	case POCO_HOST_NOT_FOUND:
		throw HostNotFoundException(arg);
	case POCO_TRY_AGAIN:
		throw DNSException("Temporary DNS error while resolving", arg);
	case POCO_NO_RECOVERY:
		throw DNSException("Non recoverable DNS error while resolving", arg);
	case POCO_NO_DATA:
		throw NoAddressFoundException(arg);
	default:
		throw IOException(NumberFormatter::format(code));
	}
}


} } // namespace Poco::Net
