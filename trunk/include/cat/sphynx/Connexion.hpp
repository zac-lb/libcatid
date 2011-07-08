/*
	Copyright (c) 2009-2011 Christopher A. Taylor.  All rights reserved.

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

#ifndef CAT_SPHYNX_CONNEXION_HPP
#define CAT_SPHYNX_CONNEXION_HPP

#include <cat/sphynx/Transport.hpp>
#include <cat/threads/WorkerThreads.hpp>
#include <cat/threads/RefObject.hpp>
#include <cat/sphynx/SphynxLayer.hpp>

namespace cat {


namespace sphynx {


// Base class for a connexion with a remote Sphynx client
class CAT_EXPORT Connexion : public Transport, public RefObject
{
	friend class Server;
	friend class ConnexionMap;

	Server *_parent; // Server object that owns this one

	NetAddr _client_addr;
	u16 _flood_key; // Flood key based on IP address, not necessarily unique
	u16 _key; // Map hash table index, unique for each active connection
	u32 _worker_id; // Worker thread index

	u8 _first_challenge[64]; // First challenge seen from this client address
	u8 _cached_answer[128]; // Cached answer to this first challenge, to avoid eating server CPU time

	// Last time a packet was received from this user -- for disconnect timeouts
	u32 _last_recv_tsc;

	// Flag indicating if a valid encrypted message has been seen yet
	bool _seen_encrypted;
	AuthenticatedEncryption _auth_enc;

	virtual bool WriteDatagrams(const BatchSet &buffers, u32 count);
	virtual void OnInternal(SphynxTLS *tls, u32 recv_time, BufferStream msg, u32 bytes);
	virtual void OnDisconnectComplete();

	virtual void OnWorkerRecv(IWorkerTLS *tls, const BatchSet &buffers);
	virtual void OnWorkerTick(IWorkerTLS *tls, u32 now);

public:
	Connexion();
	CAT_INLINE virtual ~Connexion() {}

	CAT_INLINE const NetAddr &GetAddress() { return _client_addr; }
	CAT_INLINE u16 GetKey() { return _key; }
	CAT_INLINE u16 GetFloodKey() { return _flood_key; }
	CAT_INLINE u32 GetWorkerID() { return _worker_id; }

	// Current local time
	CAT_INLINE u32 getLocalTime() { return Clock::msec(); }

	// Decompress a timestamp on server from client; byte order must be fixed before decoding
	CAT_INLINE u32 decodeClientTimestamp(u32 local_time, u16 timestamp) { return BiasedReconstructCounter<16>(local_time, TS_COMPRESS_FUTURE_TOLERANCE, timestamp); }

	// Compress timestamp on server for delivery to client; byte order must be fixed before writing to message
	CAT_INLINE u16 encodeServerTimestamp(u32 local_time) { return (u16)local_time; }

protected:
	template<class T> CAT_INLINE T *GetServer() { return reinterpret_cast<T*>( _parent ); }

	virtual void OnShutdownRequest();
	virtual bool OnZeroReferences();

	virtual void OnConnect(SphynxTLS *tls) = 0;
	virtual void OnMessages(SphynxTLS *tls, IncomingMessage msgs[], u32 count) = 0;
	virtual void OnTick(SphynxTLS *tls, u32 now) = 0;
	virtual void OnDisconnectReason(u8 reason) = 0; // Called to help explain why a disconnect is happening
};


} // namespace sphynx


} // namespace cat

#endif // CAT_SPHYNX_CONNEXION_HPP
