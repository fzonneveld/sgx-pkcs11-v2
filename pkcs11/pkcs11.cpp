#include <iostream>
#include <map>
#include "pkcs11-interface.h"


CK_SLOT_ID PKCS11_SLOT_ID = 1;
CK_SESSION_HANDLE PKCS11_SESSION_ID = 1;

CK_ULONG pkcs11_SGX_session_state = CKS_RO_PUBLIC_SESSION;
CryptoEntity *crypto=NULL;


CK_FUNCTION_LIST functionList = {
#undef CK_NEED_ARG_LIST
#define CK_PKCS11_FUNCTION_INFO(name) name,
#include "../cryptoki/pkcs11f.h"
#undef CK_PKCS11_FUNCTION_INFO
};

CK_RV C_GetFunctionList(CK_FUNCTION_LIST_PTR_PTR ppFunctionList)
{
    *ppFunctionList = &functionList;
    return CKR_OK;
}

typedef struct pkcs11_object {
	CK_ULONG ulAttributeCount;
	CK_ATTRIBUTE_PTR pAttributes;
    uint8_t *pValue;
    size_t valueLength;
} pkcs11_object_t;


typedef struct pkcs11_session {
    CK_ULONG slotID;
    CK_ULONG state;
    CK_OBJECT_HANDLE handle;
    PKCS_SGX_CK_OPERATION operation;
} pkcs11_session_t;

std::map<CK_SESSION_HANDLE, pkcs11_session_t> sessions;
static CK_ULONG sessionHandleCnt = 0;

static CK_ULONG nr_slots=0;

static pkcs11_session_t *get_session(CK_SESSION_HANDLE handle) {

    // Find session handle
    std::map<CK_SESSION_HANDLE, pkcs11_session_t>::iterator iter = sessions.find(handle);
    
    return iter != sessions.end() ? &iter->second : NULL;
}

static int get_env_int(const char *env_name, int default_value) {
    const char *env = std::getenv(env_name);
    return env == NULL ? default_value : atoi(env);
}


std::map<CK_ATTRIBUTE_TYPE, CK_ATTRIBUTE_PTR> attr2map(CK_ATTRIBUTE_PTR pAttr, CK_ULONG ulAttrCount) {
    std::map<CK_ATTRIBUTE_TYPE, CK_ATTRIBUTE_PTR> attrMap;
    do {
        attrMap[pAttr->type] = pAttr;
        pAttr++;
    } while (--ulAttrCount != 0);
    return attrMap;
}

    

CK_DEFINE_FUNCTION(CK_RV, C_Initialize)(CK_VOID_PTR pInitArgs)
{
	if (crypto != NULL) return CKR_CRYPTOKI_ALREADY_INITIALIZED;

	try {
		crypto = new CryptoEntity();
	}
	catch (std::runtime_error) {
		return CKR_DEVICE_ERROR;
	}
    // Set the slots, slots are simulated
    // Should be environment variable configurable
    nr_slots = get_env_int("PKCS_SGX_NR_SLOTS", DEFAULT_NR_SLOTS);
	return CKR_OK;
}

CK_DEFINE_FUNCTION(CK_RV, C_Finalize)(CK_VOID_PTR pReserved)
{
	if (crypto == NULL) return CKR_CRYPTOKI_NOT_INITIALIZED;
    delete(crypto);
    crypto = NULL;
	return CKR_OK;
}

CK_DEFINE_FUNCTION(CK_RV, C_GetInfo)(CK_INFO_PTR pInfo)
{
	return CKR_FUNCTION_NOT_SUPPORTED;
}

CK_DEFINE_FUNCTION(CK_RV, C_GetSlotList)(CK_BBOOL tokenPresent, CK_SLOT_ID_PTR pSlotList, CK_ULONG_PTR pulCount)
{
    int i;
	if (crypto == NULL) return CKR_CRYPTOKI_NOT_INITIALIZED;

    if (pSlotList == NULL) {
        *pulCount = nr_slots;
        return CKR_OK;
    };
    if (*pulCount > nr_slots) return CKR_SLOT_ID_INVALID;
    for (i=0; (CK_ULONG)i < *pulCount; i++) {
        pSlotList[i] = (CK_SLOT_ID) i;
    }
	return CKR_OK;;
}


#define SLOT_DESCRIPTION "SGX PKCS11 Slot %lu"
#define MANUFACTURER_ID "Intel SGX"

#define SET_STRING(d, s) { \
        memcpy(d, s, strlen(s)); \
        memset(d + strlen(s), 0, sizeof d - strlen(s)); \
    }

CK_DEFINE_FUNCTION(CK_RV, C_GetSlotInfo)(CK_SLOT_ID slotID, CK_SLOT_INFO_PTR pInfo)
{
	if (crypto == NULL) return CKR_CRYPTOKI_NOT_INITIALIZED;
    if (nr_slots <= slotID) return CKR_SLOT_ID_INVALID;
    
    char s[64] = {0};
    sprintf(s, SLOT_DESCRIPTION, slotID);
	SET_STRING(pInfo->slotDescription, s);
	SET_STRING(pInfo->manufacturerID, MANUFACTURER_ID);
    pInfo->flags = CKF_TOKEN_PRESENT;
    pInfo->hardwareVersion.major = 0x01;
    pInfo->hardwareVersion.minor = 0x00;
    pInfo->firmwareVersion.major = 0x02;
    pInfo->firmwareVersion.minor = 0x00;
	return CKR_OK;
}

#define MAX_SESSION_COUNT 100
#define MAX_RW_SESSION_COUNT 100

CK_DEFINE_FUNCTION(CK_RV, C_GetTokenInfo)(CK_SLOT_ID slotID, CK_TOKEN_INFO_PTR pInfo)
{
	if (crypto == NULL) return CKR_CRYPTOKI_NOT_INITIALIZED;
    memset(pInfo, 0, sizeof *pInfo);
    sprintf((char *)pInfo->label, "Intel SGX Token %lu", slotID);
    pInfo->flags = CKF_RNG | CKF_TOKEN_INITIALIZED;
    pInfo->ulMaxSessionCount = MAX_SESSION_COUNT;
    pInfo->ulMaxRwSessionCount = MAX_RW_SESSION_COUNT;
	return CKR_OK;;
}

CK_MECHANISM_TYPE mechanismList[] = {
    CKM_RSA_PKCS_KEY_PAIR_GEN,
};


CK_DEFINE_FUNCTION(CK_RV, C_GetMechanismList)(CK_SLOT_ID slotID, CK_MECHANISM_TYPE_PTR pMechanismList, CK_ULONG_PTR pulCount)
{
    CK_ULONG mechanismCount = sizeof mechanismList / sizeof *mechanismList;

	if (crypto == NULL) return CKR_CRYPTOKI_NOT_INITIALIZED;
    if (nr_slots <= slotID) return CKR_SLOT_ID_INVALID;
    
    if (pMechanismList == NULL) {
        *pulCount = mechanismCount;
        return CKR_OK;
    }
    if (*pulCount < mechanismCount){
        *pulCount = mechanismCount;
        return CKR_BUFFER_TOO_SMALL;
    }
    *pulCount = mechanismCount;
    memcpy(pMechanismList, mechanismList, sizeof mechanismList);
    return CKR_OK;
}


CK_DEFINE_FUNCTION(CK_RV, C_GetMechanismInfo)(CK_SLOT_ID slotID, CK_MECHANISM_TYPE type, CK_MECHANISM_INFO_PTR pInfo)
{
    switch (type) {
        case CKM_RSA_PKCS_KEY_PAIR_GEN:
            pInfo->ulMinKeySize = 1024;
            pInfo->ulMaxKeySize = 8192;
            return CKR_OK;
        default:
            return CKR_MECHANISM_INVALID;
    }
}


CK_DEFINE_FUNCTION(CK_RV, C_InitToken)(CK_SLOT_ID slotID, CK_UTF8CHAR_PTR pPin, CK_ULONG ulPinLen, CK_UTF8CHAR_PTR pLabel)
{
	return CKR_FUNCTION_NOT_SUPPORTED;
}


CK_DEFINE_FUNCTION(CK_RV, C_InitPIN)(CK_SESSION_HANDLE hSession, CK_UTF8CHAR_PTR pPin, CK_ULONG ulPinLen)
{
    pkcs11_session_t *s;
    if ((s = get_session(hSession)) == NULL) return CKR_SESSION_HANDLE_INVALID;
	return CKR_FUNCTION_NOT_SUPPORTED;
}


CK_DEFINE_FUNCTION(CK_RV, C_SetPIN)(CK_SESSION_HANDLE hSession, CK_UTF8CHAR_PTR pOldPin, CK_ULONG ulOldLen, CK_UTF8CHAR_PTR pNewPin, CK_ULONG ulNewLen)
{
	return CKR_FUNCTION_NOT_SUPPORTED;
}

CK_DEFINE_FUNCTION(CK_RV, C_OpenSession)(CK_SLOT_ID slotID, CK_FLAGS flags, CK_VOID_PTR pApplication, CK_NOTIFY Notify, CK_SESSION_HANDLE_PTR phSession)
{
	if (crypto == NULL) return CKR_CRYPTOKI_NOT_INITIALIZED;

	if (slotID >= nr_slots)
		return CKR_SLOT_ID_INVALID;

	if (!(flags & CKF_SERIAL_SESSION))
		return CKR_SESSION_PARALLEL_NOT_SUPPORTED;

	if (NULL == phSession)
		return CKR_ARGUMENTS_BAD;
    CK_FLAGS rflags = flags & CKF_RW_SESSION ? CKS_RW_PUBLIC_SESSION : CKS_RO_PUBLIC_SESSION;
    sessions[sessionHandleCnt] = {slotID, rflags};
    *phSession = sessionHandleCnt;
    sessionHandleCnt++;
	return CKR_OK;
}


CK_DEFINE_FUNCTION(CK_RV, C_CloseSession)(CK_SESSION_HANDLE hSession)
{
	if (crypto == NULL) return CKR_CRYPTOKI_NOT_INITIALIZED;

    pkcs11_session_t *s;
    if ((s = get_session(hSession)) == NULL) return CKR_SESSION_HANDLE_INVALID;
    sessions.erase(hSession);
	return CKR_OK;
}

CK_DEFINE_FUNCTION(CK_RV, C_CloseAllSessions)(CK_SLOT_ID slotID)
{
	return CKR_FUNCTION_NOT_SUPPORTED;
}


CK_DEFINE_FUNCTION(CK_RV, C_GetSessionInfo)(CK_SESSION_HANDLE hSession, CK_SESSION_INFO_PTR pInfo)
{
	return CKR_FUNCTION_NOT_SUPPORTED;
}


CK_DEFINE_FUNCTION(CK_RV, C_GetOperationState)(CK_SESSION_HANDLE hSession, CK_BYTE_PTR pOperationState, CK_ULONG_PTR pulOperationStateLen)
{
	return CKR_FUNCTION_NOT_SUPPORTED;
}


CK_DEFINE_FUNCTION(CK_RV, C_SetOperationState)(CK_SESSION_HANDLE hSession, CK_BYTE_PTR pOperationState, CK_ULONG ulOperationStateLen, CK_OBJECT_HANDLE hEncryptionKey, CK_OBJECT_HANDLE hAuthenticationKey)
{
	return CKR_FUNCTION_NOT_SUPPORTED;
}


CK_DEFINE_FUNCTION(CK_RV, C_Login)(CK_SESSION_HANDLE hSession, CK_USER_TYPE userType, CK_UTF8CHAR_PTR pPin, CK_ULONG ulPinLen)
{
	return CKR_FUNCTION_NOT_SUPPORTED;
}


CK_DEFINE_FUNCTION(CK_RV, C_Logout)(CK_SESSION_HANDLE hSession)
{
	return CKR_FUNCTION_NOT_SUPPORTED;
}


CK_DEFINE_FUNCTION(CK_RV, C_CreateObject)(CK_SESSION_HANDLE hSession, CK_ATTRIBUTE_PTR pTemplate, CK_ULONG ulCount, CK_OBJECT_HANDLE_PTR phObject)
{
	return CKR_FUNCTION_NOT_SUPPORTED;
}


CK_DEFINE_FUNCTION(CK_RV, C_CopyObject)(CK_SESSION_HANDLE hSession, CK_OBJECT_HANDLE hObject, CK_ATTRIBUTE_PTR pTemplate, CK_ULONG ulCount, CK_OBJECT_HANDLE_PTR phNewObject)
{
	return CKR_FUNCTION_NOT_SUPPORTED;
}


CK_DEFINE_FUNCTION(CK_RV, C_DestroyObject)(CK_SESSION_HANDLE hSession, CK_OBJECT_HANDLE hObject)
{
	return CKR_FUNCTION_NOT_SUPPORTED;
}


CK_DEFINE_FUNCTION(CK_RV, C_GetObjectSize)(CK_SESSION_HANDLE hSession, CK_OBJECT_HANDLE hObject, CK_ULONG_PTR pulSize)
{
	return CKR_FUNCTION_NOT_SUPPORTED;
}


CK_DEFINE_FUNCTION(CK_RV, C_GetAttributeValue)(CK_SESSION_HANDLE hSession, CK_OBJECT_HANDLE hObject, CK_ATTRIBUTE_PTR pTemplate, CK_ULONG ulCount)
{
	return CKR_FUNCTION_NOT_SUPPORTED;
}


CK_DEFINE_FUNCTION(CK_RV, C_SetAttributeValue)(CK_SESSION_HANDLE hSession, CK_OBJECT_HANDLE hObject, CK_ATTRIBUTE_PTR pTemplate, CK_ULONG ulCount)
{
	return CKR_FUNCTION_NOT_SUPPORTED;
}


CK_DEFINE_FUNCTION(CK_RV, C_FindObjectsInit)(CK_SESSION_HANDLE hSession, CK_ATTRIBUTE_PTR pTemplate, CK_ULONG ulCount)
{
	return CKR_FUNCTION_NOT_SUPPORTED;
}


CK_DEFINE_FUNCTION(CK_RV, C_FindObjects)(CK_SESSION_HANDLE hSession, CK_OBJECT_HANDLE_PTR phObject, CK_ULONG ulMaxObjectCount, CK_ULONG_PTR pulObjectCount)
{
	return CKR_FUNCTION_NOT_SUPPORTED;
}

CK_DEFINE_FUNCTION(CK_RV, C_FindObjectsFinal)(CK_SESSION_HANDLE hSession)
{
	return CKR_FUNCTION_NOT_SUPPORTED;
}

CK_DEFINE_FUNCTION(CK_RV, C_EncryptInit)(CK_SESSION_HANDLE hSession, CK_MECHANISM_PTR pMechanism, CK_OBJECT_HANDLE hKey)
{
	if (crypto == NULL) return CKR_CRYPTOKI_NOT_INITIALIZED;

    pkcs11_session_t *s;
    if ((s = get_session(hSession)) == NULL) return CKR_SESSION_HANDLE_INVALID;

	if (PKCS11_SGX_CK_OPERATION_NONE != s->operation)
		return CKR_OPERATION_ACTIVE;

	if (NULL == pMechanism)
		return CKR_ARGUMENTS_BAD;

	switch (pMechanism->mechanism)
	{
	case CKM_RSA_PKCS:

		if ((NULL != pMechanism->pParameter) || (0 != pMechanism->ulParameterLen))
			return CKR_MECHANISM_PARAM_INVALID;
		break;

	default:
		return CKR_MECHANISM_INVALID;
	}

	s->operation = PKCS11_SGX_CK_OPERATION_ENCRYPT;

	pkcs11_object_t *pub = (pkcs11_object_t *)hKey;
	crypto->RSAInitEncrypt(pub->pValue, pub->valueLength);

	return CKR_OK;
}


CK_DEFINE_FUNCTION(CK_RV, C_Encrypt)(CK_SESSION_HANDLE hSession, 
	CK_BYTE_PTR pData, CK_ULONG ulDataLen, 
	CK_BYTE_PTR pEncryptedData, CK_ULONG_PTR pulEncryptedDataLen) {

    std::cout << __FILE__ << ":" << __LINE__<< std::endl;
	if (crypto == NULL) return CKR_CRYPTOKI_NOT_INITIALIZED;

    pkcs11_session_t *s;
    if ((s = get_session(hSession)) == NULL) return CKR_SESSION_HANDLE_INVALID;

	if (PKCS11_SGX_CK_OPERATION_ENCRYPT != s->operation)
		return CKR_OPERATION_NOT_INITIALIZED;

	if (NULL == pData)
		return CKR_ARGUMENTS_BAD;

	if (0 >= ulDataLen)
		return CKR_ARGUMENTS_BAD;

	if (NULL == pulEncryptedDataLen)
		return CKR_ARGUMENTS_BAD;

	try {
        CK_ULONG len;
        CK_BYTE_PTR res = crypto->RSAEncrypt((const uint8_t *)pData, (size_t)ulDataLen, (size_t*)&len);
        if (len > *pulEncryptedDataLen) {
            free(res);
    		std::cout << __FILE__ << ":" << __LINE__<< std::endl;
            return CKR_ARGUMENTS_BAD;
        }
        memcpy(pEncryptedData, res, len);
        *pulEncryptedDataLen = len;
        free(res);
	}
	catch (std::runtime_error) {
		return CKR_DEVICE_ERROR;
	}

	s->operation = PKCS11_SGX_CK_OPERATION_NONE;
    std::cout << __FILE__ << ":" << __LINE__<< std::endl;

	return CKR_OK;
}

CK_DEFINE_FUNCTION(CK_RV, C_EncryptUpdate)(CK_SESSION_HANDLE hSession, CK_BYTE_PTR pPart, CK_ULONG ulPartLen, CK_BYTE_PTR pEncryptedPart, CK_ULONG_PTR pulEncryptedPartLen)
{
	return CKR_FUNCTION_NOT_SUPPORTED;
}

CK_DEFINE_FUNCTION(CK_RV, C_EncryptFinal)(CK_SESSION_HANDLE hSession, CK_BYTE_PTR pLastEncryptedPart, CK_ULONG_PTR pulLastEncryptedPartLen)
{
	return CKR_FUNCTION_NOT_SUPPORTED;
}

CK_DEFINE_FUNCTION(CK_RV, C_DecryptInit)(CK_SESSION_HANDLE hSession, CK_MECHANISM_PTR pMechanism, CK_OBJECT_HANDLE hKey)
{
	if (crypto == NULL) return CKR_CRYPTOKI_NOT_INITIALIZED;

    pkcs11_session_t *s;
    if ((s = get_session(hSession)) == NULL) return CKR_SESSION_HANDLE_INVALID;

	if (PKCS11_SGX_CK_OPERATION_NONE != s->operation)
		return CKR_OPERATION_ACTIVE;

	if (NULL == pMechanism)
		return CKR_ARGUMENTS_BAD;

	switch (pMechanism->mechanism)
	{
	case CKM_RSA_PKCS:

		if ((NULL != pMechanism->pParameter) || (0 != pMechanism->ulParameterLen))
			return CKR_MECHANISM_PARAM_INVALID;

		break;

		return CKR_MECHANISM_INVALID;
	}

	pkcs11_object_t *priv = (pkcs11_object_t *)hKey;
	crypto->RSAInitDecrypt(priv->pValue, priv->valueLength);
	s->operation = PKCS11_SGX_CK_OPERATION_DECRYPT;

	return CKR_OK;
}

CK_DEFINE_FUNCTION(CK_RV, C_Decrypt)(CK_SESSION_HANDLE hSession, CK_BYTE_PTR pEncryptedData, CK_ULONG ulEncryptedDataLen, CK_BYTE_PTR pData, CK_ULONG_PTR pulDataLen)
{
	if (crypto == NULL) return CKR_CRYPTOKI_NOT_INITIALIZED;

    pkcs11_session_t *s;
    if ((s = get_session(hSession)) == NULL) return CKR_SESSION_HANDLE_INVALID;

	if (PKCS11_SGX_CK_OPERATION_DECRYPT != s->operation)
		return CKR_OPERATION_NOT_INITIALIZED;

	if (NULL == pEncryptedData)
		return CKR_ARGUMENTS_BAD;

	if (0 >= ulEncryptedDataLen)
		return CKR_ARGUMENTS_BAD;

	if (NULL == pulDataLen)
		return CKR_ARGUMENTS_BAD;

	try {
        CK_ULONG resLength;
		CK_BYTE_PTR res = crypto->RSADecrypt((const CK_BYTE*)pEncryptedData, (CK_ULONG) ulEncryptedDataLen, &resLength);
        if (res == NULL) {
            return CKR_DEVICE_ERROR;
        }
        if (resLength > *pulDataLen) {
            free(res);
            return CKR_BUFFER_TOO_SMALL;
        }
        memcpy(pData, res, resLength);
        *pulDataLen = resLength;
        free(res);
	}
	catch (std::runtime_error) {
		return CKR_DEVICE_ERROR;
	}

	s->operation = PKCS11_SGX_CK_OPERATION_NONE;

	return CKR_OK;
}

CK_DEFINE_FUNCTION(CK_RV, C_DecryptUpdate)(CK_SESSION_HANDLE hSession, CK_BYTE_PTR pEncryptedPart, CK_ULONG ulEncryptedPartLen, CK_BYTE_PTR pPart, CK_ULONG_PTR pulPartLen)
{
	return CKR_FUNCTION_NOT_SUPPORTED;
}


CK_DEFINE_FUNCTION(CK_RV, C_DecryptFinal)(CK_SESSION_HANDLE hSession, CK_BYTE_PTR pLastPart, CK_ULONG_PTR pulLastPartLen)
{
	return CKR_FUNCTION_NOT_SUPPORTED;
}


CK_DEFINE_FUNCTION(CK_RV, C_DigestInit)(CK_SESSION_HANDLE hSession, CK_MECHANISM_PTR pMechanism)
{
	return CKR_FUNCTION_NOT_SUPPORTED;
}


CK_DEFINE_FUNCTION(CK_RV, C_Digest)(CK_SESSION_HANDLE hSession, CK_BYTE_PTR pData, CK_ULONG ulDataLen, CK_BYTE_PTR pDigest, CK_ULONG_PTR pulDigestLen)
{
	return CKR_FUNCTION_NOT_SUPPORTED;
}


CK_DEFINE_FUNCTION(CK_RV, C_DigestUpdate)(CK_SESSION_HANDLE hSession, CK_BYTE_PTR pPart, CK_ULONG ulPartLen)
{
	return CKR_FUNCTION_NOT_SUPPORTED;
}


CK_DEFINE_FUNCTION(CK_RV, C_DigestKey)(CK_SESSION_HANDLE hSession, CK_OBJECT_HANDLE hKey)
{
	return CKR_FUNCTION_NOT_SUPPORTED;
}


CK_DEFINE_FUNCTION(CK_RV, C_DigestFinal)(CK_SESSION_HANDLE hSession, CK_BYTE_PTR pDigest, CK_ULONG_PTR pulDigestLen)
{
	return CKR_FUNCTION_NOT_SUPPORTED;
}


CK_DEFINE_FUNCTION(CK_RV, C_SignInit)(CK_SESSION_HANDLE hSession, CK_MECHANISM_PTR pMechanism, CK_OBJECT_HANDLE hKey)
{
	return CKR_FUNCTION_NOT_SUPPORTED;
}


CK_DEFINE_FUNCTION(CK_RV, C_Sign)(CK_SESSION_HANDLE hSession, CK_BYTE_PTR pData, CK_ULONG ulDataLen, CK_BYTE_PTR pSignature, CK_ULONG_PTR pulSignatureLen)
{
	return CKR_FUNCTION_NOT_SUPPORTED;
}


CK_DEFINE_FUNCTION(CK_RV, C_SignUpdate)(CK_SESSION_HANDLE hSession, CK_BYTE_PTR pPart, CK_ULONG ulPartLen)
{
	return CKR_FUNCTION_NOT_SUPPORTED;
}


CK_DEFINE_FUNCTION(CK_RV, C_SignFinal)(CK_SESSION_HANDLE hSession, CK_BYTE_PTR pSignature, CK_ULONG_PTR pulSignatureLen)
{
	return CKR_FUNCTION_NOT_SUPPORTED;
}


CK_DEFINE_FUNCTION(CK_RV, C_SignRecoverInit)(CK_SESSION_HANDLE hSession, CK_MECHANISM_PTR pMechanism, CK_OBJECT_HANDLE hKey)
{
	return CKR_FUNCTION_NOT_SUPPORTED;
}


CK_DEFINE_FUNCTION(CK_RV, C_SignRecover)(CK_SESSION_HANDLE hSession, CK_BYTE_PTR pData, CK_ULONG ulDataLen, CK_BYTE_PTR pSignature, CK_ULONG_PTR pulSignatureLen)
{
	return CKR_FUNCTION_NOT_SUPPORTED;
}


CK_DEFINE_FUNCTION(CK_RV, C_VerifyInit)(CK_SESSION_HANDLE hSession, CK_MECHANISM_PTR pMechanism, CK_OBJECT_HANDLE hKey)
{
	return CKR_FUNCTION_NOT_SUPPORTED;
}


CK_DEFINE_FUNCTION(CK_RV, C_Verify)(CK_SESSION_HANDLE hSession, CK_BYTE_PTR pData, CK_ULONG ulDataLen, CK_BYTE_PTR pSignature, CK_ULONG ulSignatureLen)
{
	return CKR_FUNCTION_NOT_SUPPORTED;
}


CK_DEFINE_FUNCTION(CK_RV, C_VerifyUpdate)(CK_SESSION_HANDLE hSession, CK_BYTE_PTR pPart, CK_ULONG ulPartLen)
{
	return CKR_FUNCTION_NOT_SUPPORTED;
}


CK_DEFINE_FUNCTION(CK_RV, C_VerifyFinal)(CK_SESSION_HANDLE hSession, CK_BYTE_PTR pSignature, CK_ULONG ulSignatureLen)
{
	return CKR_FUNCTION_NOT_SUPPORTED;
}


CK_DEFINE_FUNCTION(CK_RV, C_VerifyRecoverInit)(CK_SESSION_HANDLE hSession, CK_MECHANISM_PTR pMechanism, CK_OBJECT_HANDLE hKey)
{
	return CKR_FUNCTION_NOT_SUPPORTED;
}


CK_DEFINE_FUNCTION(CK_RV, C_VerifyRecover)(CK_SESSION_HANDLE hSession, CK_BYTE_PTR pSignature, CK_ULONG ulSignatureLen, CK_BYTE_PTR pData, CK_ULONG_PTR pulDataLen)
{
	return CKR_FUNCTION_NOT_SUPPORTED;
}


CK_DEFINE_FUNCTION(CK_RV, C_DigestEncryptUpdate)(CK_SESSION_HANDLE hSession, CK_BYTE_PTR pPart, CK_ULONG ulPartLen, CK_BYTE_PTR pEncryptedPart, CK_ULONG_PTR pulEncryptedPartLen)
{
	return CKR_FUNCTION_NOT_SUPPORTED;
}


CK_DEFINE_FUNCTION(CK_RV, C_DecryptDigestUpdate)(CK_SESSION_HANDLE hSession, CK_BYTE_PTR pEncryptedPart, CK_ULONG ulEncryptedPartLen, CK_BYTE_PTR pPart, CK_ULONG_PTR pulPartLen)
{
	return CKR_FUNCTION_NOT_SUPPORTED;
}


CK_DEFINE_FUNCTION(CK_RV, C_SignEncryptUpdate)(CK_SESSION_HANDLE hSession, CK_BYTE_PTR pPart, CK_ULONG ulPartLen, CK_BYTE_PTR pEncryptedPart, CK_ULONG_PTR pulEncryptedPartLen)
{
	return CKR_FUNCTION_NOT_SUPPORTED;
}


CK_DEFINE_FUNCTION(CK_RV, C_DecryptVerifyUpdate)(CK_SESSION_HANDLE hSession, CK_BYTE_PTR pEncryptedPart, CK_ULONG ulEncryptedPartLen, CK_BYTE_PTR pPart, CK_ULONG_PTR pulPartLen)
{
	return CKR_FUNCTION_NOT_SUPPORTED;
}


CK_DEFINE_FUNCTION(CK_RV, C_GenerateKey)(CK_SESSION_HANDLE hSession, CK_MECHANISM_PTR pMechanism, CK_ATTRIBUTE_PTR pTemplate, CK_ULONG ulCount, CK_OBJECT_HANDLE_PTR phKey)
{
	return CKR_FUNCTION_NOT_SUPPORTED;
}


// static int check_epmty_attr(CK_ATTRIBUTE_PTR pAttr, CK_ULONG ulAttrCount){
//     CK_ULONG i;
//     for (i = 0; i < ulAttrCount; i++) {
//         if (NULL == pAttr[i].pValue || 0 >= pAttr[i].ulValueLen)
//             return 1;
//     };
//     return 0;
// }

CK_ATTRIBUTE_PTR getAttr(std::map<CK_ATTRIBUTE_TYPE, CK_ATTRIBUTE_PTR> attrMap, CK_ATTRIBUTE_TYPE type) {
    std::map<CK_ATTRIBUTE_TYPE, CK_ATTRIBUTE_PTR>::iterator it;
    it = attrMap.find(type);
    if (it == attrMap.end()) return NULL;
    return it->second;
}

CK_RV matchUlAttr(CK_ATTRIBUTE *p, CK_ULONG ul) {
    if (p) {
        if (sizeof(CK_ULONG) != p->ulValueLen) return CKR_ATTRIBUTE_VALUE_INVALID;
        if (*((CK_ULONG *)p->pValue) != ul) return CKR_TEMPLATE_INCONSISTENT;
    }
    return CKR_OK;
}

CK_BBOOL getAttrBool(std::map<CK_ATTRIBUTE_TYPE, CK_ATTRIBUTE_PTR> attrMap, CK_ATTRIBUTE_TYPE type, CK_BBOOL defaultVale, CK_BBOOL *boolValue) {
    CK_ATTRIBUTE *pAttr;
    if ((pAttr = getAttr(attrMap, type)) == NULL) {
        *boolValue = defaultVale;
    } else {
        if (pAttr->ulValueLen != sizeof(CK_BBOOL)) return CKR_ATTRIBUTE_VALUE_INVALID;
        *boolValue = *((CK_BBOOL *)pAttr->pValue);
    }
    return CKR_OK;
}
    
CK_ATTRIBUTE *map2attr(std::map<CK_ATTRIBUTE_TYPE, CK_ATTRIBUTE_PTR> m, CK_ULONG *pAttrLenth){
	int i;
	std::map<CK_ATTRIBUTE_TYPE, CK_ATTRIBUTE_PTR>::iterator it;
	CK_ATTRIBUTE *pRes;
    if ((pRes = (CK_ATTRIBUTE *)malloc(sizeof *pRes * m.size())) == NULL) {
		return NULL;
	};
	for (i=0, it=m.begin(); it != m.end(); it++, i++){
		CK_ATTRIBUTE_PTR pDest = pRes + i;
		CK_ATTRIBUTE_PTR pSrc = it->second;
		if ((pDest->pValue = malloc(pSrc->ulValueLen)) == NULL) {
			return NULL;
		}
		memcpy(pDest->pValue, pSrc->pValue, pSrc->ulValueLen);
		pDest->ulValueLen = pSrc->ulValueLen;
		pDest->type = pSrc->type;
	}
	*pAttrLenth = (CK_ULONG)i;
	return pRes;
}

		
CK_ATTRIBUTE_PTR attrMerge(
		CK_ATTRIBUTE_PTR pA, CK_ULONG aLen, CK_ATTRIBUTE_PTR  pB, CK_ULONG bLen, CK_ULONG *pAttrLength) {
	std::map<CK_ATTRIBUTE_TYPE, CK_ATTRIBUTE_PTR> a = attr2map(pA, aLen);
	std::map<CK_ATTRIBUTE_TYPE, CK_ATTRIBUTE_PTR> b = attr2map(pB, bLen);

	a.insert(b.begin(), b.end());
	CK_ATTRIBUTE_PTR ret=map2attr(a, pAttrLength);
	a.clear();
	b.clear();
	return ret;
}
    


CK_RV GenerateKeyPairRSA(
    pkcs11_session_t *session,
    std::map<CK_ATTRIBUTE_TYPE, CK_ATTRIBUTE_PTR> publicKeyAttrMap,
    std::map<CK_ATTRIBUTE_TYPE, CK_ATTRIBUTE_PTR> privateKeyAttrMap,
	CK_ATTRIBUTE_PTR pPublicKeyTemplate, CK_ULONG ulPublicKeyAttributeCount,
	CK_ATTRIBUTE_PTR pPrivateKeyTemplate, CK_ULONG ulPrivateKeyAttributeCount,
	CK_OBJECT_HANDLE_PTR phPublicKey, CK_OBJECT_HANDLE_PTR phPrivateKey) {

    if (publicKeyAttrMap.count(CKA_MODULUS_BITS) == 0) return CKR_TEMPLATE_INCONSISTENT;
    CK_ATTRIBUTE_PTR bitLenAttrPtr = publicKeyAttrMap[CKA_MODULUS_BITS];
    if (sizeof(CK_ULONG) != bitLenAttrPtr->ulValueLen) return CKR_ATTRIBUTE_VALUE_INVALID;
    CK_ULONG bitLen = *((CK_ULONG *)bitLenAttrPtr->pValue);

	uint8_t* publicKey;
	size_t publicKeyLength;
	uint8_t* privateKey;
	size_t privateKeyLength;

	try {
		crypto->RSAKeyGeneration(
			&publicKey, &publicKeyLength, &privateKey, &privateKeyLength, (size_t) bitLen);
	}
	catch (std::exception e) {
		return CKR_DEVICE_ERROR;
	}
    CK_RV ret = CKR_OK;
    CK_BBOOL token;
    if ((ret != getAttrBool(publicKeyAttrMap, CKA_TOKEN, CK_FALSE, &token)) != CKR_OK) {
        return ret;
    }
    if (token) {	
		// For now we do not store for the token
        return CKR_ATTRIBUTE_VALUE_INVALID;
    } else {
		CK_BBOOL fa = CK_FALSE;
		CK_BBOOL tr = CK_TRUE;
		CK_KEY_TYPE keyType = CKK_RSA;
        // Store in memory, return a pointer to allocated attributes...
		// Public key
		CK_OBJECT_CLASS keyClass = CKO_PUBLIC_KEY;
		CK_ATTRIBUTE publicKeyAttribs[] = {
			{ CKA_CLASS, &keyClass, sizeof keyClass },
			{ CKA_TOKEN, &token, sizeof token },
			{ CKA_PRIVATE, &fa, sizeof fa },
			{ CKA_KEY_TYPE, &keyType, sizeof keyType },
		};
		pkcs11_object_t *pub = (pkcs11_object_t *)malloc(sizeof *pub);
		pub->pAttributes = attrMerge(publicKeyAttribs, sizeof publicKeyAttribs / sizeof *publicKeyAttribs, pPublicKeyTemplate, ulPublicKeyAttributeCount, &pub->ulAttributeCount);
		pub->pValue = publicKey;
		pub->valueLength = publicKeyLength;
		*phPublicKey = (CK_ULONG)pub;
;
		// Private key
		keyClass = CKO_PRIVATE_KEY;
		CK_ATTRIBUTE privateKeyAttr[] = {
			{ CKA_CLASS, &keyClass, sizeof keyClass },
			{ CKA_TOKEN, &token, sizeof token },
			{ CKA_PRIVATE, &tr, sizeof tr },
			{ CKA_KEY_TYPE, &keyType, sizeof keyType },
		};
		pkcs11_object_t *pro = (pkcs11_object_t *)malloc(sizeof *pro);
		pro->pAttributes = attrMerge(privateKeyAttr, sizeof privateKeyAttr / sizeof *privateKeyAttr, pPrivateKeyTemplate, ulPrivateKeyAttributeCount, &pro->ulAttributeCount);
		pro->pValue = privateKey;
		pro->valueLength = privateKeyLength;
		*phPrivateKey = (CK_ULONG)pro;
    }
	return ret;
}


CK_DEFINE_FUNCTION(CK_RV, C_GenerateKeyPair)(CK_SESSION_HANDLE hSession, CK_MECHANISM_PTR pMechanism,
	CK_ATTRIBUTE_PTR pPublicKeyTemplate, CK_ULONG ulPublicKeyAttributeCount,
	CK_ATTRIBUTE_PTR pPrivateKeyTemplate, CK_ULONG ulPrivateKeyAttributeCount,
	CK_OBJECT_HANDLE_PTR phPublicKey, CK_OBJECT_HANDLE_PTR phPrivateKey)
{
	if (crypto == NULL) return CKR_CRYPTOKI_NOT_INITIALIZED;

	if (NULL == pMechanism) return CKR_ARGUMENTS_BAD;
	if (NULL == pPublicKeyTemplate) return CKR_ARGUMENTS_BAD;
	if (NULL == pPrivateKeyTemplate) return CKR_ARGUMENTS_BAD;
	if (NULL == phPublicKey) return CKR_ARGUMENTS_BAD;
	if (NULL == phPrivateKey) return CKR_ARGUMENTS_BAD;

    pkcs11_session_t *s;
    if ((s = get_session(hSession)) == NULL) return CKR_SESSION_HANDLE_INVALID;

    std::map<CK_ATTRIBUTE_TYPE, CK_ATTRIBUTE_PTR> pubKeyAttrMap;
    pubKeyAttrMap = attr2map(pPublicKeyTemplate, ulPublicKeyAttributeCount);

    CK_ATTRIBUTE_PTR pPubAttrKeyType, pPubAttrObjectClass;
    pPubAttrObjectClass  = getAttr(pubKeyAttrMap, CKA_CLASS);
    if ((pPubAttrKeyType  = getAttr(pubKeyAttrMap, CKA_KEY_TYPE)) == NULL) return CKR_TEMPLATE_INCONSISTENT;

    std::map<CK_ATTRIBUTE_TYPE, CK_ATTRIBUTE_PTR> privKeyAttrMap;
    privKeyAttrMap = attr2map(pPrivateKeyTemplate, ulPrivateKeyAttributeCount);

    CK_ATTRIBUTE_PTR pPrivAttrKeyType, pPrivAttrObjectClass;
    pPrivAttrObjectClass  = getAttr(privKeyAttrMap, CKA_CLASS);
    if ((pPrivAttrKeyType  = getAttr(privKeyAttrMap, CKA_KEY_TYPE)) == NULL) return CKR_TEMPLATE_INCONSISTENT;

    if (pPubAttrObjectClass && pPubAttrObjectClass->type != CKO_PUBLIC_KEY) return CKR_ATTRIBUTE_VALUE_INVALID;
    if (pPrivAttrObjectClass && pPrivAttrObjectClass->type != CKO_PRIVATE_KEY) return CKR_ATTRIBUTE_VALUE_INVALID;

    CK_RV ret = CKR_DEVICE_ERROR;

    switch (pMechanism->mechanism) {
        case CKM_RSA_PKCS_KEY_PAIR_GEN:
            if ((ret = matchUlAttr(pPubAttrKeyType, CKK_RSA)) != CKR_OK) return ret;
            if ((ret = matchUlAttr(pPrivAttrKeyType, CKK_RSA)) != CKR_OK) return ret;
            ret = GenerateKeyPairRSA(
                s, pubKeyAttrMap, privKeyAttrMap,
	            pPublicKeyTemplate, ulPublicKeyAttributeCount,
	            pPrivateKeyTemplate, ulPrivateKeyAttributeCount,
                phPublicKey, phPrivateKey);
			break;
        default:
            ret = CKR_MECHANISM_INVALID;
    }
    pubKeyAttrMap.clear();
    privKeyAttrMap.clear();
    return ret;
}

CK_DEFINE_FUNCTION(CK_RV, C_WrapKey)(CK_SESSION_HANDLE hSession, CK_MECHANISM_PTR pMechanism, CK_OBJECT_HANDLE hWrappingKey, CK_OBJECT_HANDLE hKey, CK_BYTE_PTR pWrappedKey, CK_ULONG_PTR pulWrappedKeyLen)
{
	return CKR_FUNCTION_NOT_SUPPORTED;
}


CK_DEFINE_FUNCTION(CK_RV, C_UnwrapKey)(CK_SESSION_HANDLE hSession, CK_MECHANISM_PTR pMechanism, CK_OBJECT_HANDLE hUnwrappingKey, CK_BYTE_PTR pWrappedKey, CK_ULONG ulWrappedKeyLen, CK_ATTRIBUTE_PTR pTemplate, CK_ULONG ulAttributeCount, CK_OBJECT_HANDLE_PTR phKey)
{
	return CKR_FUNCTION_NOT_SUPPORTED;
}


CK_DEFINE_FUNCTION(CK_RV, C_DeriveKey)(CK_SESSION_HANDLE hSession, CK_MECHANISM_PTR pMechanism, CK_OBJECT_HANDLE hBaseKey, CK_ATTRIBUTE_PTR pTemplate, CK_ULONG ulAttributeCount, CK_OBJECT_HANDLE_PTR phKey)
{
	return CKR_FUNCTION_NOT_SUPPORTED;
}


CK_DEFINE_FUNCTION(CK_RV, C_SeedRandom)(CK_SESSION_HANDLE hSession, CK_BYTE_PTR pSeed, CK_ULONG ulSeedLen)
{
	return CKR_FUNCTION_NOT_SUPPORTED;
}


CK_DEFINE_FUNCTION(CK_RV, C_GenerateRandom)(CK_SESSION_HANDLE hSession, CK_BYTE_PTR RandomData, CK_ULONG ulRandomLen)
{
	return CKR_FUNCTION_NOT_SUPPORTED;
}


CK_DEFINE_FUNCTION(CK_RV, C_GetFunctionStatus)(CK_SESSION_HANDLE hSession)
{
	return CKR_FUNCTION_NOT_SUPPORTED;
}


CK_DEFINE_FUNCTION(CK_RV, C_CancelFunction)(CK_SESSION_HANDLE hSession)
{
	return CKR_FUNCTION_NOT_SUPPORTED;
}


CK_DEFINE_FUNCTION(CK_RV, C_WaitForSlotEvent)(CK_FLAGS flags, CK_SLOT_ID_PTR pSlot, CK_VOID_PTR pReserved)
{
	return CKR_FUNCTION_NOT_SUPPORTED;
}
