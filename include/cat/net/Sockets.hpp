/*
	Copyright (c) 2009-2010 Christopher A. Taylor.  All rights reserved.

	Redistribution and use in source and binary forms, with or without
	modification, are permitted provided that the following conditions are met:

	* Redistributions of source code must retain the above copyright notice,
	  this list of conditions and the following disclaimer.
	* Redistributions in binary form must reproduce the above copyright notice,
	  this list of conditions and the following disclaimer in the documentation
	  and/or other materials provided with the distribution.
	* Neither the name of LibCat nor the names of its contributors may be used
	  to endorse or promote products derived from this software without
	  specific prior written permission.

	THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
	AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
	IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
	ARE DISCLAIMED.  IN NO EVENT SHALL THE COPYRIGHT HOLDER OR CONTRIBUTORS BE
	LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
	CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
	SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
	INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
	CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
	ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
	POSSIBILITY OF SUCH DAMAGE.
*/

#ifndef CAT_SOCKETS_HPP
#define CAT_SOCKETS_HPP

#include <cat/lang/RefSingleton.hpp>
#include <string>

#if defined(CAT_OS_WINDOWS)
# include <WS2tcpip.h>
#else
# include <unistd.h>
#endif

#define CAT_LOOPBACK_IP4 "127.0.0.1"
#define CAT_LOOPBACK_IP6 "::1"

namespace cat {


//// Data Types

#if defined(CAT_OS_WINDOWS)
	typedef SOCKET Socket;
	CAT_INLINE bool CloseSocket(Socket s) { return !closesocket(s); }
#else
	typedef int Socket;
	static const Socket INVALID_SOCKET = -1;
	static const int SOCKET_ERROR = -1;
	CAT_INLINE bool CloseSocket(Socket s) { return !close(s); }
#endif

typedef u16 Port;

#pragma pack(push)
#pragma pack(1)

// Base version of NetAddr that has no ctors so that it can be used in a union
struct CAT_EXPORT UNetAddr
{
	union
	{
		u8 v6_bytes[16];
		u16 v6_words[8];
		u64 v6[2];
		struct {
			u32 v4;
			u32 v4_padding[3];
		};
	} _ip; // Network order

	union
	{
		u32 _valid;
		struct {
			Port _port; // Host order
			u16 _family; // Host order
		};
	};

	static const int IP4_BYTES = 4;
	static const int IP6_BYTES = 16;

	typedef sockaddr_in6 SockAddr;

	// These functions are designed to support when this object overlaps the
	// memory space of the input
	bool Wrap(const sockaddr_in &addr);
	bool Wrap(const sockaddr *addr);

	CAT_INLINE bool Wrap(const sockaddr_in6 &addr)
	{
		// May be IPv4 that has been stuffed into an IPv6 sockaddr
		return Wrap(reinterpret_cast<const sockaddr*>( &addr ));
	}

	// Promote an IPv4 address to an IPv6 address if needed
	bool PromoteTo6();

	// Check if an IPv6 address can be demoted to IPv4 address
	bool CanDemoteTo4() const;

	// Demote an IPv6 address to an IPv4 address if possible,
	// otherwise marks address as invalid and returns false
	bool DemoteTo4();

	CAT_INLINE bool Convert(bool To6) { if (To6) return PromoteTo6(); else return DemoteTo4(); }

	CAT_INLINE bool Valid() const { return _valid != 0; }
	CAT_INLINE bool Is6() const { return _family == AF_INET6; }

	CAT_INLINE const u32 GetIP4() const { return _ip.v4; }
	CAT_INLINE const u64 *GetIP6() const { return _ip.v6; }

	CAT_INLINE Port GetPort() const { return _port; }
	CAT_INLINE void SetPort(Port port) { _port = port; }

	// Mark the address as invalid
	CAT_INLINE void Invalidate() { _valid = 0; }

	bool EqualsIPOnly(const UNetAddr &addr) const;

	CAT_INLINE bool operator==(const UNetAddr &addr) const
	{
		// Check port
		if (addr._port != _port)
			return false; // "not equal"

		// Tail call IP checking function
		return EqualsIPOnly(addr);
	}

	CAT_INLINE bool operator!=(const UNetAddr &addr) const
	{
		return !(*this == addr);
	}

	// To validate external input; don't want clients connecting
	// to their local network instead of the actual game server.
	bool IsInternetRoutable();

	// Returns true if the address is routable on local network or Internet.
	// Returns false if the address is IPv4 multicast, loopback, or weird.
	bool IsRoutable();

	bool SetFromString(const char *ip_str, Port port = 0);
	std::string IPToString() const;

	bool SetFromRawIP(const u8 *ip_binary, int bytes);
	bool SetFromDotDecimals(int a, int b, int c, int d, Port port = 0);

	bool Unwrap(SockAddr &addr, int &addr_len, bool PromoteToIP6 = false) const;
};

// Wrapper for IPv4 and IPv6 addresses
struct CAT_EXPORT NetAddr : UNetAddr
{
	CAT_INLINE NetAddr() {}

	CAT_INLINE NetAddr(const char *ip_str, Port port = 0)
	{
		// Invoke SetFromString(), ignoring the return value because
		// it will leave the object in an invalid state if needed.
		SetFromString(ip_str, port);
	}
	CAT_INLINE NetAddr(const sockaddr_in6 &addr)
	{
		Wrap(addr);
	}
	CAT_INLINE NetAddr(const sockaddr_in &addr)
	{
		Wrap(addr);
	}
	CAT_INLINE NetAddr(const sockaddr *addr)
	{
		Wrap(addr);
	}
	CAT_INLINE NetAddr(int a, int b, int c, int d, Port port = 0)
	{
		// Invoke SetFromDotDecimals(), ignoring the return value because
		// it will leave the object in an invalid state if needed.
		SetFromDotDecimals(a, b, c, d, port);
	}

	CAT_INLINE NetAddr(const NetAddr &addr)
	{
		_valid = addr._valid;
		_ip.v6[0] = addr._ip.v6[0];
		_ip.v6[1] = addr._ip.v6[1];
	}

	CAT_INLINE NetAddr &operator=(const NetAddr &addr)
	{
		_valid = addr._valid;
		_ip.v6[0] = addr._ip.v6[0];
		_ip.v6[1] = addr._ip.v6[1];
		return *this;
	}
};

#pragma pack(pop)


//// Sockets

class Sockets : RefSingleton<Sockets>

// Run startup and cleanup functions needed under some OS
CAT_EXPORT bool StartupSockets(); // returns false on error
CAT_EXPORT void CleanupSockets();

// inout_OnlyIPv4: Indicates that only IPv4 is requested by caller
// Sets OnlyIPv4 if IPv6 will be unsupported
// Returns true on success
CAT_EXPORT bool CreateSocket(int type, int protocol, bool SupportIPv4, Socket &out_s, bool &inout_OnlyIPv4);

// Returns true on success
CAT_EXPORT bool NetBind(Socket s, Port port, bool OnlyIPv4);

// Returns 0 on failure
CAT_EXPORT Port GetBoundPort(Socket s);


//// Error Codes

// Returns a string describing the last error from Winsock2 API
CAT_EXPORT std::string SocketGetLastErrorString();
CAT_EXPORT std::string SocketGetErrorString(int code);


} // namespace cat

#endif // CAT_SOCKETS_HPP
