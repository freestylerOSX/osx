/*
 * Copyright (c) 2002-2004,2012 Apple Inc. All Rights Reserved.
 * 
 * @APPLE_LICENSE_HEADER_START@
 * 
 * This file contains Original Code and/or Modifications of Original Code
 * as defined in and that are subject to the Apple Public Source License
 * Version 2.0 (the 'License'). You may not use this file except in
 * compliance with the License. Please obtain a copy of the License at
 * http://www.opensource.apple.com/apsl/ and read it before using this
 * file.
 * 
 * The Original Code and all software distributed under the License are
 * distributed on an 'AS IS' basis, WITHOUT WARRANTY OF ANY KIND, EITHER
 * EXPRESS OR IMPLIED, AND APPLE HEREBY DISCLAIMS ALL SUCH WARRANTIES,
 * INCLUDING WITHOUT LIMITATION, ANY WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE, QUIET ENJOYMENT OR NON-INFRINGEMENT.
 * Please see the License for the specific language governing rights and
 * limitations under the License.
 * 
 * @APPLE_LICENSE_HEADER_END@
 */

#ifndef _SECURITY_SECCERTIFICATEPRIV_H_
#define _SECURITY_SECCERTIFICATEPRIV_H_

#include <Security/SecBase.h>
#include <Security/cssmtype.h>
#include <Security/x509defs.h>
#include <CoreFoundation/CFBase.h>
#include <CoreFoundation/CFArray.h>
#include <CoreFoundation/CFData.h>
#include <CoreFoundation/CFDate.h>

#if defined(__cplusplus)
extern "C" {
#endif

/* Deprecated; use SecCertificateCopyCommonName() instead. */
OSStatus SecCertificateGetCommonName(SecCertificateRef certificate, CFStringRef *commonName);

/* Deprecated; use SecCertificateCopyEmailAddresses() instead. */
/* This should have been Copy instead of Get since the returned address is not autoreleased. */
OSStatus SecCertificateGetEmailAddress(SecCertificateRef certificate, CFStringRef *emailAddress);

/*
 * Private API to infer a display name for a SecCertificateRef which
 * may or may not be in a keychain.
 * This is a candidate for deprecation; use SecItemCopyDisplayNames() instead.
 */
OSStatus SecCertificateInferLabel(SecCertificateRef certificate, CFStringRef *label);

/*
 * Subset of the above, useful for both certs and CRLs.
 * Infer printable label for a given an CSSM_X509_NAME. Returns NULL
 * if no appropriate printable name found.
 */
const CSSM_DATA *SecInferLabelFromX509Name(
	const CSSM_X509_NAME *x509Name);

/* Accessors for fields in the cached certificate */

/*!
	@function SecCertificateCopyFieldValues
	@abstract Retrieves the values for a particular field in a given certificate.
    @param certificate A valid SecCertificateRef to the certificate.
    @param field Pointer to the OID whose values should be returned.
    @param fieldValues On return, a zero terminated list of CSSM_DATA_PTR's.
	@result A result code.  See "Security Error Codes" (SecBase.h).
	@discussion Return a zero terminated list of CSSM_DATA_PTR's with the
	values of the field specified by field.  Caller must call
	SecCertificateReleaseFieldValues to free the storage allocated by this call.
*/
OSStatus SecCertificateCopyFieldValues(SecCertificateRef certificate, const CSSM_OID *field, CSSM_DATA_PTR **fieldValues);

/*!
	@function SecCertificateReleaseFieldValues
	@abstract Release the storage associated with the values returned by SecCertificateCopyFieldValues.
    @param certificate A valid SecCertificateRef to the certificate.
    @param field Pointer to the OID whose values were returned by SecCertificateCopyFieldValues.
    @param fieldValues Pointer to a zero terminated list of CSSM_DATA_PTR's.
	@result A result code.  See "Security Error Codes" (SecBase.h).
	@discussion Release the storage associated with the values returned by SecCertificateCopyFieldValues.
*/
OSStatus SecCertificateReleaseFieldValues(SecCertificateRef certificate, const CSSM_OID *field, CSSM_DATA_PTR *fieldValues);

/*!
	@function SecCertificateCopyFirstFieldValue
	@abstract Return a CSSM_DATA_PTR with the value of the first field specified by field.
    @param certificate A valid SecCertificateRef to the certificate.
    @param field Pointer to the OID whose value should be returned.
    @param fieldValue On return, a CSSM_DATA_PTR to the field data.
	@result A result code.  See "Security Error Codes" (SecBase.h).
	@discussion Return a CSSM_DATA_PTR with the value of the first field specified by field.  Caller must call
	SecCertificateReleaseFieldValue to free the storage allocated by this call.
*/
OSStatus SecCertificateCopyFirstFieldValue(SecCertificateRef certificate, const CSSM_OID *field, CSSM_DATA_PTR *fieldValue);

/*!
	@function SecCertificateReleaseFirstFieldValue
	@abstract Release the storage associated with the values returned by SecCertificateCopyFirstFieldValue.
    @param certificate A valid SecCertificateRef to the certificate.
    @param field Pointer to the OID whose values were returned by SecCertificateCopyFieldValue.
    @param fieldValue The field data to release.
	@result A result code.  See "Security Error Codes" (SecBase.h).
	@discussion Release the storage associated with the values returned by SecCertificateCopyFieldValue.
*/
OSStatus SecCertificateReleaseFirstFieldValue(SecCertificateRef certificate, const CSSM_OID *field, CSSM_DATA_PTR fieldValue);

/*!
    @function SecCertificateCopySubjectComponent
    @abstract Retrieves a component of the subject distinguished name of a given certificate.
    @param certificate A reference to the certificate from which to retrieve the common name.
	@param component A component oid naming the component desired. See <Security/oidsattr.h>.
    @param result On return, a reference to the string form of the component, if present in the subject.
		Your code must release this reference by calling the CFRelease function.
    @result A result code. See "Security Error Codes" (SecBase.h).
 */
OSStatus SecCertificateCopySubjectComponent(SecCertificateRef certificate, const CSSM_OID *component,
	CFStringRef *result);


/*	Convenience functions for searching.
*/

OSStatus SecCertificateFindByIssuerAndSN(CFTypeRef keychainOrArray, const CSSM_DATA *issuer,
	const CSSM_DATA *serialNumber, 	SecCertificateRef *certificate);

OSStatus SecCertificateFindBySubjectKeyID(CFTypeRef keychainOrArray, const CSSM_DATA *subjectKeyID,
	SecCertificateRef *certificate);

OSStatus SecCertificateFindByEmail(CFTypeRef keychainOrArray, const char *emailAddress,
	SecCertificateRef *certificate);


/* These should go to SecKeychainSearchPriv.h. */
OSStatus SecKeychainSearchCreateForCertificateByIssuerAndSN(CFTypeRef keychainOrArray, const CSSM_DATA *issuer,
	const CSSM_DATA *serialNumber, SecKeychainSearchRef *searchRef);

OSStatus SecKeychainSearchCreateForCertificateByIssuerAndSN_CF(CFTypeRef keychainOrArray, CFDataRef issuer,
	CFDataRef serialNumber, SecKeychainSearchRef *searchRef);

OSStatus SecKeychainSearchCreateForCertificateBySubjectKeyID(CFTypeRef keychainOrArray, const CSSM_DATA *subjectKeyID,
	SecKeychainSearchRef *searchRef);

OSStatus SecKeychainSearchCreateForCertificateByEmail(CFTypeRef keychainOrArray, const char *emailAddress,
	SecKeychainSearchRef *searchRef);

/* Convenience function for generating digests; should be moved elsewhere. */
CSSM_RETURN SecDigestGetData(CSSM_ALGORITHMS alg, CSSM_DATA* digest, const CSSM_DATA* data);

/* Return true iff certificate is valid as of verifyTime. */
bool SecCertificateIsValidX(SecCertificateRef certificate, CFAbsoluteTime verifyTime);

/* NOT EXPORTED YET; copied from SecurityInterface but could be useful in the future.
CSSM_CSP_HANDLE	SecGetAppleCSPHandle();
CSSM_CL_HANDLE SecGetAppleCLHandle();
*/

/* determine whether a cert is self-signed */
OSStatus SecCertificateIsSelfSigned(
	SecCertificateRef certRef,
	Boolean *isSelfSigned);		/* RETURNED */


#if defined(__cplusplus)
}
#endif

#endif /* !_SECURITY_SECCERTIFICATEPRIV_H_ */
