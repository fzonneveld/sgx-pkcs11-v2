#include <cstring>
#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <stdarg.h>

#include "crypto_engine_t.h"
#include "tSgxSSL_api.h"
#include "sgx_urts.h"
#include "sgx_tseal.h"
#include "sgx_tcrypto.h"
#include "sgx_trts.h"

#include "openssl/rsa.h"
#include "openssl/pem.h"
#include "openssl/rand.h"

#define CK_PTR *
#define CK_DEFINE_FUNCTION(returnType, name) returnType name
#define CK_DECLARE_FUNCTION(returnType, name) returnType name
#define CK_DECLARE_FUNCTION_POINTER(returnType, name) returnType (* name)
#define CK_CALLBACK_FUNCTION(returnType, name) returnType (* name)

#ifndef NULL_PTR
#define NULL_PTR 0
#endif

#include "../cryptoki/pkcs11.h"

#define ROOTKEY_LENGTH 16
static uint8_t rootKey[ROOTKEY_LENGTH];
CK_BBOOL rootKeySet = CK_FALSE;

RSA *generateRSA(size_t bits, const uint8_t *exponent, size_t exponentLength) {
	RSA *ret = NULL;
	BIGNUM *bne = NULL;

	unsigned long e = RSA_F4;

	if ((bne = BN_new()) == NULL) return NULL;
	if (exponent == NULL) {
		if (BN_set_word(bne, e) != 1) goto generateRSA_err;
	} else {
		if (BN_bin2bn(exponent, exponentLength, bne) == NULL) goto generateRSA_err;
	}

	if ((ret = RSA_new()) == NULL) goto generateRSA_err;
	if ((RSA_generate_key_ex(ret, (int)bits, bne, NULL)) != 1) {
        RSA_free(ret);
        ret = NULL;
    }
generateRSA_err:
    BN_free(bne);
    return ret;
}

typedef int (*i2d_pkey)(EVP_PKEY *a, unsigned char **pp);

int getRSAder(const RSA *r, uint8_t **ppRSAder, i2d_pkey f){
    EVP_PKEY *pKey = EVP_PKEY_new();
    int ret = -1;
    if (*ppRSAder != NULL) return -1;
    if (1 != EVP_PKEY_set1_RSA(pKey, (RSA *)r)) goto getRSAder_err;
    if ((ret = f(pKey, ppRSAder)) <= 0) goto getRSAder_err;
    goto  getRSAder_good;
getRSAder_err:
    if (*ppRSAder) free(*ppRSAder);
getRSAder_good:
    if (pKey) EVP_PKEY_free(pKey);
    return ret;
}


int SGXgenerateRSAKeyPair(
        uint8_t *RSAPublicKey, size_t RSAPublicKeyLength, size_t *RSAPublicKeyLengthOut,
        uint8_t *RSAPrivateKey, size_t RSAPrivateKeyLength, size_t *RSAPrivateKeyLengthOut,
		const uint8_t *pSerialAttr, size_t serialAttrLen,
        const unsigned char *exponent, size_t exponentLength,
        size_t bitLen) {
    int ret = -1;
    RSA *rsa_key = NULL;
    size_t privateKeyDERlength, publicKeyLength;
    uint8_t *pPrivateKeyDER = NULL, *pPublicKeyDER = NULL;

    if (rootKeySet == CK_FALSE) return ret;

	if ((rsa_key = generateRSA(bitLen, exponent, exponentLength)) == NULL) goto generateRSAKeyPair_err;
	ret = -2;
    if ((privateKeyDERlength = getRSAder(rsa_key, &pPrivateKeyDER, i2d_PrivateKey)) <= 0) goto generateRSAKeyPair_err;
	ret = -3;
    if (0 >= (publicKeyLength = getRSAder(rsa_key, &pPublicKeyDER, i2d_PublicKey))) goto generateRSAKeyPair_err;
	ret = -4;
    if (publicKeyLength > RSAPublicKeyLength) goto generateRSAKeyPair_err;

    if ((privateKeyDERlength  + SGX_AESGCM_MAC_SIZE + SGX_AESGCM_IV_SIZE) > RSAPrivateKeyLength) goto generateRSAKeyPair_err;

	if (SGX_SUCCESS != sgx_read_rand(RSAPrivateKey + SGX_AESGCM_MAC_SIZE, SGX_AESGCM_IV_SIZE)) goto generateRSAKeyPair_err;

	if (SGX_SUCCESS != sgx_rijndael128GCM_encrypt(
		(sgx_aes_gcm_128bit_key_t *) rootKey,
		pPrivateKeyDER, privateKeyDERlength, 
		RSAPrivateKey + SGX_AESGCM_MAC_SIZE + SGX_AESGCM_IV_SIZE,
		RSAPrivateKey + SGX_AESGCM_MAC_SIZE,
		SGX_AESGCM_IV_SIZE,
		//pSerialAttr, serialAttrLen,
		NULL, 0,
		(sgx_aes_gcm_128bit_tag_t *) (RSAPrivateKey))) goto generateRSAKeyPair_err;
	
    *RSAPublicKeyLengthOut = publicKeyLength;
    *RSAPrivateKeyLengthOut = privateKeyDERlength + SGX_AESGCM_MAC_SIZE + SGX_AESGCM_IV_SIZE;
    memcpy(RSAPublicKey, pPublicKeyDER, publicKeyLength);
    ret = 0;
generateRSAKeyPair_err:
    if (pPrivateKeyDER) free(pPrivateKeyDER);
    if (pPublicKeyDER) free(pPublicKeyDER);
    if (rsa_key) RSA_free(rsa_key);
    return ret;
}

int SGXEncryptRSA(
        const uint8_t* public_key, size_t public_key_length,
        const uint8_t* plaintext, size_t plaintext_length,
        uint8_t* ciphertext, size_t ciphertext_length,
        size_t* cipherTextLength) {

	int padding = RSA_PKCS1_PADDING;
    int len;
    int ret = -1;
    RSA *rsa = NULL;
    EVP_PKEY *pKey = NULL;
	if (NULL == (pKey = EVP_PKEY_new())) goto SGXEncryptRSA_err;
	if (NULL == (pKey = d2i_PublicKey(EVP_PKEY_RSA, &pKey, &public_key, public_key_length))) goto SGXEncryptRSA_err;
	if (NULL == (rsa = EVP_PKEY_get1_RSA(pKey))) goto SGXEncryptRSA_err;

	if (( len = RSA_public_encrypt(
            plaintext_length, (uint8_t*)plaintext, (unsigned char*)ciphertext, rsa, padding)) == -1)
        goto SGXEncryptRSA_err;

	*cipherTextLength = (size_t)len;
    ret = 0;
SGXEncryptRSA_err:
    if (rsa) RSA_free(rsa);
	if (pKey) EVP_PKEY_free(pKey);
    return ret;
}

int SGXDecryptRSA(
        const uint8_t *private_key_ciphered,
        size_t private_key_ciphered_length,
        const uint8_t* ciphertext,
        size_t ciphertext_length,
        uint8_t* plaintext,
        size_t plaintext_length,
        size_t *plainTextLength) {

    uint8_t *to = NULL;
    int ret = -1;
	int padding = RSA_PKCS1_PADDING;
	RSA *rsa = NULL;
    int to_len = -1;
	EVP_PKEY *pKey = NULL;
	const uint8_t *endptr;
	uint8_t *private_key_der = NULL;
	size_t privateKeyDERlength;

    if (rootKeySet == CK_FALSE) return ret;

	if (private_key_ciphered_length < (SGX_AESGCM_MAC_SIZE + SGX_AESGCM_IV_SIZE)) goto SGXDecryptRSA_err;
	privateKeyDERlength = private_key_ciphered_length - (SGX_AESGCM_MAC_SIZE + SGX_AESGCM_IV_SIZE);

	if (NULL == (private_key_der = (uint8_t *)malloc(privateKeyDERlength))) goto SGXDecryptRSA_err;
	ret = -2;
	if (SGX_SUCCESS != sgx_rijndael128GCM_decrypt(
		(sgx_aes_gcm_128bit_key_t *) rootKey,
		private_key_ciphered + SGX_AESGCM_MAC_SIZE + SGX_AESGCM_IV_SIZE,
		privateKeyDERlength,
		private_key_der,
		private_key_ciphered + SGX_AESGCM_MAC_SIZE,
		SGX_AESGCM_IV_SIZE,
		NULL, 0,
		(sgx_aes_gcm_128bit_tag_t *) private_key_ciphered)) goto SGXDecryptRSA_err;
	ret  = -3;
	endptr = (const uint8_t *) private_key_der;
	if ((pKey = d2i_PrivateKey(EVP_PKEY_RSA, &pKey, &endptr, (long) privateKeyDERlength)) == NULL)
		goto SGXDecryptRSA_err;
	if (NULL == (rsa = EVP_PKEY_get1_RSA(pKey))) goto SGXDecryptRSA_err;
    if ((to = (uint8_t *)malloc(RSA_size(rsa))) == NULL) goto SGXDecryptRSA_err;
	if (-1 == (to_len = RSA_private_decrypt(ciphertext_length, ciphertext, to, rsa, padding)))
		goto SGXDecryptRSA_err;
    ret = -6;
    if ((size_t) to_len > plaintext_length) goto SGXDecryptRSA_err;
    ret = -7;
    memcpy(plaintext, to, to_len);
    *plainTextLength = to_len;
    ret = 0;
SGXDecryptRSA_err:
	if (to) free(to);
	if (private_key_der) free(private_key_der);
    if (pKey) EVP_PKEY_free(pKey);
    return ret;
}

size_t SGXGetSealedRootKeySize(){
    return sizeof(sgx_sealed_data_t) + ROOTKEY_LENGTH;
}

int SGXGenerateRootKey(uint8_t *rootKeySealed, size_t root_key_length, size_t *rootKeyLength){
    uint32_t sealedSize;
	sgx_status_t stat;
    rootKeySet = CK_FALSE;
    if (!RAND_bytes(rootKey, sizeof rootKey)) {
        return -1;
    }
    if ((sealedSize = sgx_calc_sealed_data_size(0, sizeof rootKey)) == UINT32_MAX)
        return -1;
    if (sealedSize > root_key_length)
        return -1;
    if ((SGX_SUCCESS != (stat = sgx_seal_data(
            0, NULL, sizeof(rootKey), (const uint8_t *)rootKey, root_key_length, (sgx_sealed_data_t *)rootKeySealed))))
        return -1;
    rootKeySet = CK_TRUE;
    return 0;
}

int SGXGetRootKeySealed(uint8_t *root_key_sealed, size_t root_key_len_sealed, size_t *rootKeyLenSealed){
	sgx_status_t stat;
    static uint8_t rootKey[ROOTKEY_LENGTH];

    *rootKeyLenSealed = SGXGetSealedRootKeySize();
    if (*rootKeyLenSealed > root_key_len_sealed) {
        return -1;
    }
    if ((SGX_SUCCESS != (stat = sgx_seal_data(
            0, NULL, sizeof(rootKey), (const uint8_t *)rootKey, sizeof rootKey, (sgx_sealed_data_t *)root_key_sealed))))
        return -1;
    return 0;
}


int SGXSetRootKeySealed(const uint8_t *root_key_sealed, size_t root_key_len_sealed){
    uint32_t decrypted_text_length = sizeof rootKey;
	sgx_status_t stat;

    rootKeySet = CK_FALSE;
    if ((SGX_SUCCESS != (stat = sgx_unseal_data(
            (const sgx_sealed_data_t *)root_key_sealed,
            NULL, NULL,
            rootKey, &decrypted_text_length))))
        return -1;
    rootKeySet = CK_TRUE;
    return 0;
}
