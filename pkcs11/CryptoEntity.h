#pragma once
#ifndef POLITO_CSS_ESIGNER_H_
#define POLITO_CSS_ESIGNER_H_

#include <sgx_urts.h>
#include <string>
#include "crypto_engine_u.h"
#include "shared_values.h"

#define CK_PTR *
#define CK_DEFINE_FUNCTION(returnType, name) returnType name
#define CK_DECLARE_FUNCTION(returnType, name) returnType name
#define CK_DECLARE_FUNCTION_POINTER(returnType, name) returnType (* name)
#define CK_CALLBACK_FUNCTION(returnType, name) returnType (* name)

#ifndef NULL_PTR
#define NULL_PTR 0
#endif

#include "../cryptoki/pkcs11.h"

class CryptoEntity {
private:
#ifdef _WIN32
	const char* kEnclaveFile = "PKCS11_crypto_engine.signed.dll";
#else
	const char* kEnclaveFile = "PKCS11_crypto_engine.signed.so";
#endif
	const char* kTokenFile = "token";
	sgx_enclave_id_t enclave_id_;
public:
	CryptoEntity();
    void KeyGeneration(uint8_t **pPublicKey, size_t *pPublicKeyLength, uint8_t **publicSerializedAttr, size_t *pPubAttrLen, uint8_t **pPrivateKey, size_t *pPrivateKeyLength, uint8_t **privSerializedAttr, size_t *pPrivAttrLen);
	// void RSAInitEncrypt(uint8_t* key, size_t length);

	uint8_t *Sign(const uint8_t *key, size_t keyLength, uint8_t *pAttribute, size_t attributeLen, const uint8_t *pData, size_t dataLen, size_t *pSignatureLen, CK_MECHANISM_TYPE mechanism);
	uint8_t* RSADecrypt(const uint8_t *key, size_t keyLength, const uint8_t* cipherData, size_t cipherDataLength, size_t* plainLength);
    uint8_t* RSADecrypt(const uint8_t *key, size_t keyLength, uint8_t *pAttribute, size_t attributeLen, const uint8_t* cipherData, size_t cipherDataLength, size_t* plainLength);
    int GenerateRandom(uint8_t *random, size_t random_length);
    size_t GetSealedRootKeySize();
    int GenerateRootKey(uint8_t *rootKeySealed, size_t *rootKeySealedLength);
    int RestoreRootKey(uint8_t *rootKeySealed, size_t rootKeySealedLength);
	~CryptoEntity();
};

#endif  // POLITO_CSS_ESIGNER_H_
