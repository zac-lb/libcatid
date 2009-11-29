/*
	Copyright (c) 2009 Christopher A. Taylor.  All rights reserved.

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

#include <cat/crypt/tunnel/KeyAgreementResponder.hpp>
#include <cat/crypt/tunnel/AuthenticatedEncryption.hpp>
#include <cat/crypt/SecureCompare.hpp>
#include <cat/port/AlignedAlloc.hpp>
#include <cat/time/Clock.hpp>
using namespace cat;

bool KeyAgreementResponder::AllocateMemory()
{
    FreeMemory();

    b = new (Aligned::ii) Leg[KeyLegs * 15];
    B = b + KeyLegs;
	B_neutral = B + KeyLegs*2;
	y[0] = B_neutral + KeyLegs*2;
	y[1] = y[0] + KeyLegs;
	Y_neutral[0] = y[1] + KeyLegs;
	Y_neutral[1] = Y_neutral[0] + KeyLegs*4;

    return !!b;
}

void KeyAgreementResponder::FreeMemory()
{
    if (b)
    {
        memset(b, 0, KeyBytes);
        memset(y[0], 0, KeyBytes);
        memset(y[1], 0, KeyBytes);
        Aligned::Delete(b);
        b = 0;
    }

	if (G_MultPrecomp)
	{
		Aligned::Delete(G_MultPrecomp);
		G_MultPrecomp = 0;
	}
}

KeyAgreementResponder::KeyAgreementResponder()
{
    b = 0;
    G_MultPrecomp = 0;

#if defined(CAT_NO_ATOMIC_RESPONDER)
	m_mutex_created = false;
#endif // CAT_NO_ATOMIC_RESPONDER
}

KeyAgreementResponder::~KeyAgreementResponder()
{
#if defined(CAT_NO_ATOMIC_RESPONDER)
	if (m_mutex_created) pthread_mutex_destroy(&m_thread_id_mutex);
#endif // CAT_NO_ATOMIC_RESPONDER

	FreeMemory();
}

void KeyAgreementResponder::Rekey(BigTwistedEdwards *math, FortunaOutput *csprng)
{
	// NOTE: This function is very fragile because it has to be thread-safe
	u32 NextY = ActiveY ^ 1;

	// y = ephemeral key
	GenerateKey(math, csprng, y[NextY]);

	// Y = y * G
	Leg *Y = Y_neutral[NextY];
	math->PtMultiply(G_MultPrecomp, 8, y[NextY], 0, Y);
	math->SaveAffineXY(Y, Y, Y + KeyLegs);

	ActiveY = NextY;

#if defined(CAT_NO_ATOMIC_RESPONDER)

	pthread_mutex_lock(&m_thread_id_mutex);
	ChallengeCount = 0;
	pthread_mutex_unlock(&m_thread_id_mutex);

#else // CAT_NO_ATOMIC_RESPONDER

	Atomic::Set(&ChallengeCount, 0);

#endif // CAT_NO_ATOMIC_RESPONDER
}

bool KeyAgreementResponder::Initialize(BigTwistedEdwards *math, FortunaOutput *csprng,
									   const u8 *responder_public_key, int public_bytes,
									   const u8 *responder_private_key, int private_bytes)
{
#if defined(CAT_USER_ERROR_CHECKING)
	if (!math || !csprng) return false;
#endif

#if defined(CAT_NO_ATOMIC_RESPONDER)
	if (!m_mutex_created)
	{
		if (pthread_mutex_init(&m_thread_id_mutex, 0) != 0)
			return false;
		m_mutex_created = true;
	}
#endif // CAT_NO_ATOMIC_RESPONDER

	int bits = math->RegBytes() * 8;

    // Validate and accept number of bits
    if (!KeyAgreementCommon::Initialize(bits))
        return false;

    // Allocate memory space for the responder's key pair and generator point
    if (!AllocateMemory())
        return false;

    // Verify that inputs are of the correct length
    if (private_bytes != KeyBytes) return false;
    if (public_bytes != KeyBytes*2) return false;

	// Precompute an 8-bit table for multiplication
	G_MultPrecomp = math->PtMultiplyPrecompAlloc(8);
    if (!G_MultPrecomp) return false;
    math->PtMultiplyPrecomp(math->GetGenerator(), 8, G_MultPrecomp);

    // Unpack the responder's public point
    math->Load(responder_private_key, KeyBytes, b);
    if (!math->LoadVerifyAffineXY(responder_public_key, responder_public_key+KeyBytes, B))
        return false;
    math->PtUnpack(B);

	// Verify public point is not identity element
	if (math->IsAffineIdentity(B))
		return false;

	// Store a copy of the endian-neutral version of B for later
	memcpy(B_neutral, responder_public_key, KeyBytes*2);

	// Initialize re-keying
	ChallengeCount = 0;
	ActiveY = 0;
	Rekey(math, csprng);

    return true;
}

bool KeyAgreementResponder::ProcessChallenge(BigTwistedEdwards *math, FortunaOutput *csprng,
											 const u8 *initiator_challenge, int challenge_bytes,
                                             u8 *responder_answer, int answer_bytes, Skein *key_hash)
{
#if defined(CAT_USER_ERROR_CHECKING)
	// Verify that inputs are of the correct length
	if (!math || !csprng || challenge_bytes != KeyBytes*2 || answer_bytes != KeyBytes*4)
		return false;
#endif

    Leg *A = math->Get(0);
    Leg *S = math->Get(8);
    Leg *T = math->Get(12);
    Leg *hA = math->Get(16);

    // Unpack the initiator's A into registers
    if (!math->LoadVerifyAffineXY(initiator_challenge, initiator_challenge + KeyBytes, A))
        return false;

	// Verify the point is not the additive identity (should never happen unless being attacked)
	if (math->IsAffineIdentity(A))
		return false;

    // hA = h * A for small subgroup attack resistance
    math->PtDoubleZ1(A, hA);
    math->PtEDouble(hA, hA);

#if defined(CAT_NO_ATOMIC_RESPONDER)

	bool time_to_rekey = false;

	pthread_mutex_lock(&m_thread_id_mutex);
	// Check if it is time to rekey
	if (ChallengeCount++ == 100)
		time_to_rekey = true;
	pthread_mutex_unlock(&m_thread_id_mutex);

	if (time_to_rekey)
		Rekey(math, csprng);

#else // CAT_NO_ATOMIC_RESPONDER

	// Check if it is time to rekey
	if (Atomic::Add(&ChallengeCount, 1) == 100)
		Rekey(math, csprng);

#endif // CAT_NO_ATOMIC_RESPONDER

	// Copy the current endian neutral Y to the responder answer
	u32 ThisY = ActiveY;
	memcpy(responder_answer, Y_neutral[ThisY], KeyBytes*2);

	do
	{
		// random n-bit number r
		csprng->Generate(responder_answer + KeyBytes*2, KeyBytes);

		// S = H(A,B,Y,r)
		if (!key_hash->BeginKey(KeyBits))
			return false;
		key_hash->Crunch(initiator_challenge, KeyBytes*2); // A
		key_hash->Crunch(B_neutral, KeyBytes*2); // B
		key_hash->Crunch(responder_answer, KeyBytes*3); // Y,r
		key_hash->End();
		key_hash->Generate(S, KeyBytes);
		math->Load(S, KeyBytes, S);

		// Repeat while S is small
	} while (math->LessX(S, 1000));

	// T = S*y + b (mod q)
	math->MulMod(S, y[ThisY], math->GetCurveQ(), T);
	if (math->Add(T, b, T))
		math->Subtract(T, math->GetCurveQ(), T);
	while (!math->Less(T, math->GetCurveQ()))
		math->Subtract(T, math->GetCurveQ(), T);

	// T = AffineX(T * hA)
	math->PtMultiply(hA, T, 0, S);
    math->SaveAffineX(S, T);

	// k = H(d,T)
	if (!key_hash->BeginKDF())
		return false;
	key_hash->Crunch(T, KeyBytes);
	key_hash->End();

	return true;
}

bool KeyAgreementResponder::Sign(BigTwistedEdwards *math, FortunaOutput *csprng,
								 const u8 *message, int message_bytes,
								 u8 *signature, int signature_bytes)
{
#if defined(CAT_USER_ERROR_CHECKING)
	// Verify that inputs are of the correct length
	if (!math || !csprng || signature_bytes != KeyBytes*2) return false;
#endif

    Leg *k = math->Get(0);
    Leg *K = math->Get(1);
    Leg *e = math->Get(5);
    Leg *s = math->Get(6);

    // k = ephemeral key
	GenerateKey(math, csprng, k);

	// K = k * G
	math->PtMultiply(G_MultPrecomp, 8, k, 0, K);
	math->SaveAffineX(K, K);

	do {

		do {
			// e = H(M || K)
			Skein H;

			if (!H.BeginKey(KeyBits)) return false;
			H.Crunch(message, message_bytes);
			H.Crunch(K, KeyBytes);
			H.End();
			H.Generate(signature, KeyBytes);

			math->Load(signature, KeyBytes, e);

			// e = e (mod q), for checking if it is congruent to q
			while (!math->Less(e, math->GetCurveQ()))
				math->Subtract(e, math->GetCurveQ(), e);

		} while (math->IsZero(e));

		// s = b * e (mod q)
		math->MulMod(b, e, math->GetCurveQ(), s);

		// s = -s (mod q)
		if (!math->IsZero(s)) math->Subtract(math->GetCurveQ(), s, s);

		// s = s + k (mod q)
		if (math->Add(s, k, s))
			while (!math->Subtract(s, math->GetCurveQ(), s));
		while (!math->Less(s, math->GetCurveQ()))
			math->Subtract(s, math->GetCurveQ(), s);

	} while (math->IsZero(s));

	math->Save(s, signature + KeyBytes, KeyBytes);

	// Erase the ephemeral secret from memory
	math->CopyX(0, k);

	return true;
}