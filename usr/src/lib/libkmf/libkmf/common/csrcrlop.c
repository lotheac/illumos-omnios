/*
 * CDDL HEADER START
 *
 * The contents of this file are subject to the terms of the
 * Common Development and Distribution License (the "License").
 * You may not use this file except in compliance with the License.
 *
 * You can obtain a copy of the license at usr/src/OPENSOLARIS.LICENSE
 * or http://www.opensolaris.org/os/licensing.
 * See the License for the specific language governing permissions
 * and limitations under the License.
 *
 * When distributing Covered Code, include this CDDL HEADER in each
 * file and include the License file at usr/src/OPENSOLARIS.LICENSE.
 * If applicable, add the following below this CDDL HEADER, with the
 * fields enclosed by brackets "[]" replaced with your own identifying
 * information: Portions Copyright [yyyy] [name of copyright owner]
 *
 * CDDL HEADER END
 *
 * Copyright 2010 Sun Microsystems, Inc.  All rights reserved.
 * Use is subject to license terms.
 */

#include <stdio.h>
#include <link.h>
#include <fcntl.h>
#include <ctype.h>
#include <sys/param.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <sys/socket.h>

#include <ber_der.h>
#include <kmfapiP.h>

#include <pem_encode.h>
#include <libgen.h>
#include <cryptoutil.h>

static KMF_RETURN
setup_crl_call(KMF_HANDLE_T, int, KMF_ATTRIBUTE *, KMF_PLUGIN **);

/*
 *
 * Name: kmf_set_csr_pubkey
 *
 * Description:
 *   This function converts the specified plugin public key to SPKI form,
 *   and save it in the KMF_CSR_DATA internal structure
 *
 * Parameters:
 *   KMFkey(input) - pointer to the KMF_KEY_HANDLE structure containing the
 *		public key generated by the plug-in CreateKeypair
 *   Csr(input/output) - pointer to a KMF_CSR_DATA structure containing
 *		SPKI
 *
 * Returns:
 *   A KMF_RETURN value indicating success or specifying a particular
 *   error condition.
 *   The value KMF_OK indicates success. All other values represent
 *   an error condition.
 *
 */
KMF_RETURN
kmf_set_csr_pubkey(KMF_HANDLE_T handle,
	KMF_KEY_HANDLE *KMFKey,
	KMF_CSR_DATA *Csr)
{
	KMF_RETURN ret;
	KMF_X509_SPKI *spki_ptr;
	KMF_PLUGIN *plugin;
	KMF_DATA KeyData = { 0, NULL };

	CLEAR_ERROR(handle, ret);
	if (ret != KMF_OK)
		return (ret);

	if (KMFKey == NULL || Csr == NULL) {
		return (KMF_ERR_BAD_PARAMETER);
	}

	/* The keystore must extract the pubkey data */
	plugin = FindPlugin(handle, KMFKey->kstype);
	if (plugin != NULL && plugin->funclist->EncodePubkeyData != NULL) {
		ret = plugin->funclist->EncodePubkeyData(handle,
		    KMFKey, &KeyData);
	} else {
		return (KMF_ERR_PLUGIN_NOTFOUND);
	}

	spki_ptr = &Csr->csr.subjectPublicKeyInfo;

	ret = DerDecodeSPKI(&KeyData, spki_ptr);

	kmf_free_data(&KeyData);

	return (ret);
}

KMF_RETURN
kmf_set_csr_version(KMF_CSR_DATA *CsrData, uint32_t version)
{
	if (CsrData == NULL)
		return (KMF_ERR_BAD_PARAMETER);

	/*
	 * From RFC 3280:
	 * Version  ::=  INTEGER  {  v1(0), v2(1), v3(2)  }
	 */
	if (version != 0 && version != 1 && version != 2)
		return (KMF_ERR_BAD_PARAMETER);
	return (set_integer(&CsrData->csr.version, (void *)&version,
	    sizeof (uint32_t)));
}

KMF_RETURN
kmf_set_csr_subject(KMF_CSR_DATA *CsrData,
	KMF_X509_NAME *subject_name_ptr)
{
	KMF_RETURN rv = KMF_OK;
	KMF_X509_NAME *temp_name_ptr = NULL;

	if (CsrData != NULL && subject_name_ptr != NULL) {
		rv = CopyRDN(subject_name_ptr, &temp_name_ptr);
		if (rv == KMF_OK) {
			CsrData->csr.subject = *temp_name_ptr;
		}
	} else {
		return (KMF_ERR_BAD_PARAMETER);
	}
	return (rv);
}
KMF_RETURN
kmf_create_csr_file(KMF_DATA *csrdata, KMF_ENCODE_FORMAT format,
	char *csrfile)
{
	KMF_RETURN rv = KMF_OK;
	int fd = -1;
	KMF_DATA pemdata = { 0, NULL };

	if (csrdata == NULL || csrfile == NULL)
		return (KMF_ERR_BAD_PARAMETER);

	if (format != KMF_FORMAT_PEM && format != KMF_FORMAT_ASN1)
		return (KMF_ERR_BAD_PARAMETER);

	if (format == KMF_FORMAT_PEM) {
		int len;
		rv = kmf_der_to_pem(KMF_CSR,
		    csrdata->Data, csrdata->Length,
		    &pemdata.Data, &len);
		if (rv != KMF_OK)
			goto cleanup;
		pemdata.Length = (size_t)len;
	}

	if ((fd = open(csrfile, O_CREAT |O_RDWR, 0644)) == -1) {
		rv = KMF_ERR_OPEN_FILE;
		goto cleanup;
	}

	if (format == KMF_FORMAT_PEM) {
		if (write(fd, pemdata.Data, pemdata.Length) !=
		    pemdata.Length) {
			rv = KMF_ERR_WRITE_FILE;
		}
	} else {
		if (write(fd, csrdata->Data, csrdata->Length) !=
		    csrdata->Length) {
			rv = KMF_ERR_WRITE_FILE;
		}
	}

cleanup:
	if (fd != -1)
		(void) close(fd);

	kmf_free_data(&pemdata);

	return (rv);
}

KMF_RETURN
kmf_set_csr_extn(KMF_CSR_DATA *Csr, KMF_X509_EXTENSION *extn)
{
	KMF_RETURN ret = KMF_OK;
	KMF_X509_EXTENSIONS *exts;

	if (Csr == NULL || extn == NULL)
		return (KMF_ERR_BAD_PARAMETER);

	exts = &Csr->csr.extensions;

	ret = add_an_extension(exts, extn);

	return (ret);
}

KMF_RETURN
kmf_set_csr_sig_alg(KMF_CSR_DATA *CsrData,
	KMF_ALGORITHM_INDEX sigAlg)
{
	KMF_OID	*alg;

	if (CsrData == NULL)
		return (KMF_ERR_BAD_PARAMETER);

	alg = x509_algid_to_algoid(sigAlg);

	if (alg != NULL) {
		(void) copy_data((KMF_DATA *)
		    &CsrData->signature.algorithmIdentifier.algorithm,
		    (KMF_DATA *)alg);
		(void) copy_data(
		    &CsrData->signature.algorithmIdentifier.parameters,
		    &CsrData->csr.subjectPublicKeyInfo.algorithm.parameters);
	} else {
		return (KMF_ERR_BAD_PARAMETER);
	}
	return (KMF_OK);
}

KMF_RETURN
kmf_set_csr_subject_altname(KMF_CSR_DATA *Csr,
	char *altname, int critical,
	KMF_GENERALNAMECHOICES alttype)
{
	KMF_RETURN ret = KMF_OK;

	if (Csr == NULL || altname == NULL)
		return (KMF_ERR_BAD_PARAMETER);

	ret = kmf_set_altname(&Csr->csr.extensions,
	    (KMF_OID *)&KMFOID_SubjectAltName, critical, alttype,
	    altname);

	return (ret);
}

KMF_RETURN
kmf_set_csr_ku(KMF_CSR_DATA *CSRData,
	int critical, uint16_t kubits)
{
	KMF_RETURN ret = KMF_OK;

	if (CSRData == NULL)
		return (KMF_ERR_BAD_PARAMETER);

	ret = set_key_usage_extension(
	    &CSRData->csr.extensions, critical, kubits);

	return (ret);
}

KMF_RETURN
kmf_add_csr_eku(KMF_CSR_DATA *CSRData, KMF_OID *ekuOID,
	int critical)
{
	KMF_RETURN ret = KMF_OK;
	KMF_X509_EXTENSION *foundextn;
	KMF_X509_EXTENSION newextn;
	BerElement *asn1 = NULL;
	BerValue *extdata = NULL;
	char *olddata = NULL;
	size_t oldsize = 0;
	KMF_X509EXT_EKU ekudata;

	if (CSRData == NULL || ekuOID == NULL)
		return (KMF_ERR_BAD_PARAMETER);

	(void) memset(&ekudata, 0, sizeof (KMF_X509EXT_EKU));
	(void) memset(&newextn, 0, sizeof (newextn));

	foundextn = FindExtn(&CSRData->csr.extensions,
	    (KMF_OID *)&KMFOID_ExtendedKeyUsage);
	if (foundextn != NULL) {
		ret = GetSequenceContents((char *)foundextn->BERvalue.Data,
		    foundextn->BERvalue.Length, &olddata, &oldsize);
		if (ret != KMF_OK)
			goto out;

		/*
		 * If the EKU is already in the cert, then just return OK.
		 */
		ret = parse_eku_data(&foundextn->BERvalue, &ekudata);
		if (ret == KMF_OK) {
			if (is_eku_present(&ekudata, ekuOID)) {
				goto out;
			}
		}
	}
	if ((asn1 = kmfder_alloc()) == NULL)
		return (KMF_ERR_MEMORY);

	if (kmfber_printf(asn1, "{") == -1) {
		ret = KMF_ERR_ENCODING;
		goto out;
	}

	/* Write the old extension data first */
	if (olddata != NULL && oldsize > 0) {
		if (kmfber_write(asn1, olddata, oldsize, 0) == -1) {
			ret = KMF_ERR_ENCODING;
			goto out;
		}
	}

	/* Append this EKU OID and close the sequence */
	if (kmfber_printf(asn1, "D}", ekuOID) == -1) {
		ret = KMF_ERR_ENCODING;
		goto out;
	}

	if (kmfber_flatten(asn1, &extdata) == -1) {
		ret = KMF_ERR_ENCODING;
		goto out;
	}

	/*
	 * If we are just adding to an existing list of EKU OIDs,
	 * just replace the BER data associated with the found extension.
	 */
	if (foundextn != NULL) {
		free(foundextn->BERvalue.Data);
		foundextn->critical = critical;
		foundextn->BERvalue.Data = (uchar_t *)extdata->bv_val;
		foundextn->BERvalue.Length = extdata->bv_len;
	} else {
		ret = copy_data(&newextn.extnId,
		    (KMF_DATA *)&KMFOID_ExtendedKeyUsage);
		if (ret != KMF_OK)
			goto out;
		newextn.critical = critical;
		newextn.format = KMF_X509_DATAFORMAT_ENCODED;
		newextn.BERvalue.Data = (uchar_t *)extdata->bv_val;
		newextn.BERvalue.Length = extdata->bv_len;
		ret = kmf_set_csr_extn(CSRData, &newextn);
		if (ret != KMF_OK)
			free(newextn.BERvalue.Data);
	}

out:
	kmf_free_eku(&ekudata);
	if (extdata != NULL)
		free(extdata);

	if (olddata != NULL)
		free(olddata);

	if (asn1 != NULL)
		kmfber_free(asn1, 1);

	if (ret != KMF_OK)
		kmf_free_data(&newextn.extnId);

	return (ret);
}

static KMF_RETURN
sign_csr(KMF_HANDLE_T handle,
	const KMF_DATA *SubjectCsr,
	KMF_KEY_HANDLE	*Signkey,
	KMF_X509_ALGORITHM_IDENTIFIER *algo,
	KMF_DATA	*SignedCsr)
{
	KMF_CSR_DATA	subj_csr;
	KMF_TBS_CSR	*tbs_csr = NULL;
	KMF_DATA	signed_data = { 0, NULL };
	KMF_RETURN	ret = KMF_OK;
	KMF_ATTRIBUTE	attlist[5];
	KMF_ALGORITHM_INDEX algid;
	int i = 0;

	if (!SignedCsr)
		return (KMF_ERR_BAD_PARAMETER);

	SignedCsr->Length = 0;
	SignedCsr->Data = NULL;

	if (!SubjectCsr)
		return (KMF_ERR_BAD_PARAMETER);

	if (!SubjectCsr->Data || !SubjectCsr->Length)
		return (KMF_ERR_BAD_PARAMETER);

	(void) memset(&subj_csr, 0, sizeof (subj_csr));
	/* Estimate the signed data length generously */
	signed_data.Length = SubjectCsr->Length*2;
	signed_data.Data = calloc(1, signed_data.Length);
	if (!signed_data.Data) {
		ret = KMF_ERR_MEMORY;
		goto cleanup;
	}

	kmf_set_attr_at_index(attlist, i++,
	    KMF_KEYSTORE_TYPE_ATTR, &Signkey->kstype,
	    sizeof (Signkey->kstype));

	kmf_set_attr_at_index(attlist, i++,
	    KMF_KEY_HANDLE_ATTR, Signkey, sizeof (KMF_KEY_HANDLE));

	kmf_set_attr_at_index(attlist, i++, KMF_OID_ATTR, &algo->algorithm,
	    sizeof (KMF_OID));

	kmf_set_attr_at_index(attlist, i++, KMF_DATA_ATTR,
	    (KMF_DATA *)SubjectCsr, sizeof (KMF_DATA));

	kmf_set_attr_at_index(attlist, i++, KMF_OUT_DATA_ATTR,
	    &signed_data, sizeof (KMF_DATA));

	ret = kmf_sign_data(handle, i, attlist);
	if (KMF_OK != ret)
		goto cleanup;
	/*
	 * If we got here OK, decode into a structure and then re-encode
	 * the complete CSR.
	 */
	ret = DerDecodeTbsCsr(SubjectCsr, &tbs_csr);
	if (ret)
		goto cleanup;

	(void) memcpy(&subj_csr.csr, tbs_csr, sizeof (KMF_TBS_CSR));

	ret = copy_algoid(&subj_csr.signature.algorithmIdentifier, algo);
	if (ret)
		goto cleanup;

	algid = x509_algoid_to_algid(&algo->algorithm);
	if (algid == KMF_ALGID_SHA1WithDSA ||
	    algid == KMF_ALGID_SHA256WithDSA ||
	    algid == KMF_ALGID_SHA1WithECDSA ||
	    algid == KMF_ALGID_SHA256WithECDSA ||
	    algid == KMF_ALGID_SHA384WithECDSA ||
	    algid == KMF_ALGID_SHA512WithECDSA) {
		/*
		 * For DSA and ECDSA, we must encode the
		 * signature correctly.
		 */
		KMF_DATA signature;

		ret = DerEncodeDSASignature(&signed_data, &signature);
		kmf_free_data(&signed_data);

		if (ret != KMF_OK)
			goto cleanup;

		subj_csr.signature.encrypted = signature;
	} else {
		subj_csr.signature.encrypted = signed_data;
	}

	/* Now, re-encode the CSR with the new signature */
	ret = DerEncodeSignedCsr(&subj_csr, SignedCsr);
	if (ret != KMF_OK) {
		kmf_free_data(SignedCsr);
		goto cleanup;
	}

	/* Cleanup & return */
cleanup:
	free(tbs_csr);

	kmf_free_tbs_csr(&subj_csr.csr);

	kmf_free_algoid(&subj_csr.signature.algorithmIdentifier);
	kmf_free_data(&signed_data);

	return (ret);
}

/*
 *
 * Name: kmf_sign_csr
 *
 * Description:
 *   This function signs a CSR and returns the result as a
 *   signed, encoded CSR in SignedCsr
 *
 * Parameters:
 *   tbsCsr(input) - pointer to a KMF_DATA structure containing a
 *		DER encoded TBS CSR data
 *   Signkey(input) - pointer to the KMF_KEY_HANDLE structure containing
 *		the private key generated by the plug-in CreateKeypair
 *   algo(input) - contains algorithm info needed for signing
 *   SignedCsr(output) - pointer to the KMF_DATA structure containing
 *		the signed CSR
 *
 * Returns:
 *   A KMF_RETURN value indicating success or specifying a particular
 *   error condition.
 *   The value KMF_OK indicates success. All other values represent
 *   an error condition.
 *
 */
KMF_RETURN
kmf_sign_csr(KMF_HANDLE_T handle,
	const KMF_CSR_DATA *tbsCsr,
	KMF_KEY_HANDLE	*Signkey,
	KMF_DATA	*SignedCsr)
{
	KMF_RETURN err;
	KMF_DATA csrdata = { 0, NULL };

	CLEAR_ERROR(handle, err);
	if (err != KMF_OK)
		return (err);

	if (tbsCsr == NULL || Signkey == NULL || SignedCsr == NULL)
		return (KMF_ERR_BAD_PARAMETER);

	SignedCsr->Data = NULL;
	SignedCsr->Length = 0;

	err = DerEncodeTbsCsr((KMF_TBS_CSR *)&tbsCsr->csr, &csrdata);
	if (err == KMF_OK) {
		err = sign_csr(handle, &csrdata, Signkey,
		    (KMF_X509_ALGORITHM_IDENTIFIER *)
		    &tbsCsr->signature.algorithmIdentifier,
		    SignedCsr);
	}

	if (err != KMF_OK) {
		kmf_free_data(SignedCsr);
	}
	kmf_free_data(&csrdata);
	return (err);
}

/*
 * kmf_decode_csr
 *
 * Description:
 *   This function decodes raw CSR data and fills in the KMF_CSR_DATA
 *   record.
 *
 * Inputs:
 *	KMF_HANDLE_T handle
 *	KMF_DATA *rawcsr
 *	KMF_CSR_DATA *csrdata;
 */
KMF_RETURN
kmf_decode_csr(KMF_HANDLE_T handle, KMF_DATA *rawcsr, KMF_CSR_DATA *csrdata)
{
	KMF_RETURN rv;
	KMF_CSR_DATA *cdata = NULL;

	if (handle == NULL || rawcsr == NULL || csrdata == NULL)
		return (KMF_ERR_BAD_PARAMETER);

	rv = DerDecodeSignedCsr(rawcsr, &cdata);
	if (rv != KMF_OK)
		return (rv);

	(void) memcpy(csrdata, cdata, sizeof (KMF_CSR_DATA));

	free(cdata);
	return (rv);
}

KMF_RETURN
kmf_verify_csr(KMF_HANDLE_T handle, int numattr,
	KMF_ATTRIBUTE *attrlist)
{
	KMF_RETURN rv = KMF_OK;
	KMF_CSR_DATA *csrdata = NULL;
	KMF_ALGORITHM_INDEX algid;
	KMF_X509_ALGORITHM_IDENTIFIER *x509alg;
	KMF_DATA rawcsr;

	KMF_ATTRIBUTE_TESTER required_attrs[] = {
	    {KMF_CSR_DATA_ATTR, FALSE, sizeof (KMF_CSR_DATA),
	    sizeof (KMF_CSR_DATA)},
	};

	int num_req_attrs = sizeof (required_attrs) /
	    sizeof (KMF_ATTRIBUTE_TESTER);

	if (handle == NULL)
		return (KMF_ERR_BAD_PARAMETER);

	CLEAR_ERROR(handle, rv);

	rv = test_attributes(num_req_attrs, required_attrs,
	    0, NULL, numattr, attrlist);
	if (rv != KMF_OK)
		return (rv);

	csrdata = kmf_get_attr_ptr(KMF_CSR_DATA_ATTR, attrlist, numattr);
	if (csrdata == NULL)
		return (KMF_ERR_BAD_PARAMETER);

	rv = DerEncodeTbsCsr(&csrdata->csr, &rawcsr);
	if (rv != KMF_OK)
		return (rv);

	x509alg = &csrdata->signature.algorithmIdentifier;
	algid = x509_algoid_to_algid(&x509alg->algorithm);
	if (algid == KMF_ALGID_SHA1WithDSA ||
	    algid == KMF_ALGID_SHA256WithDSA) {
		/* Decode the DSA signature before verifying it */
		KMF_DATA signature;
		rv = DerDecodeDSASignature(&csrdata->signature.encrypted,
		    &signature);
		if (rv != KMF_OK)
			goto end;

		rv = PKCS_VerifyData(handle, algid,
		    &csrdata->csr.subjectPublicKeyInfo,
		    &rawcsr, &signature);

		kmf_free_data(&signature);
	} else if (algid == KMF_ALGID_SHA1WithECDSA ||
	    algid == KMF_ALGID_SHA256WithECDSA ||
	    algid == KMF_ALGID_SHA384WithECDSA ||
	    algid == KMF_ALGID_SHA512WithECDSA) {
		KMF_DATA signature;
		rv = DerDecodeECDSASignature(&csrdata->signature.encrypted,
		    &signature);
		if (rv != KMF_OK)
			goto end;

		rv = PKCS_VerifyData(handle, algid,
		    &csrdata->csr.subjectPublicKeyInfo,
		    &rawcsr, &signature);

		kmf_free_data(&signature);
	} else {
		rv = PKCS_VerifyData(handle, algid,
		    &csrdata->csr.subjectPublicKeyInfo,
		    &rawcsr,
		    &csrdata->signature.encrypted);
	}

end:
	kmf_free_data(&rawcsr);
	return (rv);
}

static KMF_RETURN
setup_crl_call(KMF_HANDLE_T handle, int numattr,
	KMF_ATTRIBUTE *attrlist, KMF_PLUGIN **plugin)
{
	KMF_RETURN ret;
	KMF_KEYSTORE_TYPE kstype;
	uint32_t len = sizeof (kstype);

	KMF_ATTRIBUTE_TESTER required_attrs[] = {
	    {KMF_KEYSTORE_TYPE_ATTR, FALSE, 1, sizeof (KMF_KEYSTORE_TYPE)}
	};

	int num_req_attrs = sizeof (required_attrs) /
	    sizeof (KMF_ATTRIBUTE_TESTER);

	if (handle == NULL || plugin == NULL)
		return (KMF_ERR_BAD_PARAMETER);

	CLEAR_ERROR(handle, ret);

	ret = test_attributes(num_req_attrs, required_attrs,
	    0, NULL, numattr, attrlist);
	if (ret != KMF_OK)
		return (ret);

	ret = kmf_get_attr(KMF_KEYSTORE_TYPE_ATTR, attrlist, numattr,
	    &kstype, &len);
	if (ret != KMF_OK)
		return (ret);

	switch (kstype) {
	case KMF_KEYSTORE_NSS:
		*plugin = FindPlugin(handle, kstype);
		break;

	case KMF_KEYSTORE_OPENSSL:
	case KMF_KEYSTORE_PK11TOKEN: /* PKCS#11 CRL is file-based */
		*plugin = FindPlugin(handle, KMF_KEYSTORE_OPENSSL);
		break;
	default:
		return (KMF_ERR_PLUGIN_NOTFOUND);
	}
	return (KMF_OK);
}

KMF_RETURN
kmf_import_crl(KMF_HANDLE_T handle, int numattr, KMF_ATTRIBUTE *attrlist)
{
	KMF_RETURN ret;
	KMF_PLUGIN *plugin;

	ret = setup_crl_call(handle, numattr, attrlist, &plugin);
	if (ret != KMF_OK)
		return (ret);

	if (plugin == NULL)
		return (KMF_ERR_PLUGIN_NOTFOUND);
	else if (plugin->funclist->ImportCRL != NULL)
		return (plugin->funclist->ImportCRL(handle, numattr, attrlist));

	return (KMF_ERR_FUNCTION_NOT_FOUND);
}

KMF_RETURN
kmf_delete_crl(KMF_HANDLE_T handle, int numattr, KMF_ATTRIBUTE *attrlist)
{
	KMF_RETURN ret;
	KMF_PLUGIN *plugin;

	ret = setup_crl_call(handle, numattr, attrlist, &plugin);
	if (ret != KMF_OK)
		return (ret);

	if (plugin == NULL)
		return (KMF_ERR_PLUGIN_NOTFOUND);
	else if (plugin->funclist->DeleteCRL != NULL)
		return (plugin->funclist->DeleteCRL(handle, numattr, attrlist));

	return (KMF_ERR_FUNCTION_NOT_FOUND);
}

KMF_RETURN
kmf_list_crl(KMF_HANDLE_T handle, int numattr, KMF_ATTRIBUTE *attrlist)
{
	KMF_PLUGIN *plugin;
	KMF_RETURN ret;

	ret = setup_crl_call(handle, numattr, attrlist, &plugin);
	if (ret != KMF_OK)
		return (ret);

	if (plugin == NULL)
		return (KMF_ERR_PLUGIN_NOTFOUND);
	else if (plugin->funclist->ListCRL != NULL)
		return (plugin->funclist->ListCRL(handle, numattr, attrlist));
	return (KMF_ERR_FUNCTION_NOT_FOUND);
}

KMF_RETURN
kmf_find_crl(KMF_HANDLE_T handle, int numattr, KMF_ATTRIBUTE *attrlist)
{
	KMF_PLUGIN *plugin;
	KMF_RETURN ret;
	KMF_KEYSTORE_TYPE kstype;
	uint32_t len = sizeof (kstype);

	KMF_ATTRIBUTE_TESTER required_attrs[] = {
	    {KMF_KEYSTORE_TYPE_ATTR, FALSE, 1,
	    sizeof (KMF_KEYSTORE_TYPE)},
	    {KMF_CRL_COUNT_ATTR, FALSE,
	    sizeof (char *), sizeof (char *)}
	};

	int num_req_attrs = sizeof (required_attrs) /
	    sizeof (KMF_ATTRIBUTE_TESTER);
	if (handle == NULL)
		return (KMF_ERR_BAD_PARAMETER);

	CLEAR_ERROR(handle, ret);

	ret = test_attributes(num_req_attrs, required_attrs,
	    0, NULL, numattr, attrlist);
	if (ret != KMF_OK)
		return (ret);

	ret = kmf_get_attr(KMF_KEYSTORE_TYPE_ATTR, attrlist, numattr,
	    &kstype, &len);
	if (ret != KMF_OK)
		return (ret);

	switch (kstype) {
	case KMF_KEYSTORE_NSS:
		plugin = FindPlugin(handle, kstype);
		break;
	case KMF_KEYSTORE_OPENSSL:
	case KMF_KEYSTORE_PK11TOKEN:
		return (KMF_ERR_FUNCTION_NOT_FOUND);
	default:
		/*
		 * FindCRL is only implemented for NSS. PKCS#11
		 * and file-based keystores just store in a file
		 * and don't need a "Find" function.
		 */
		return (KMF_ERR_PLUGIN_NOTFOUND);
	}

	if (plugin == NULL)
		return (KMF_ERR_PLUGIN_NOTFOUND);
	else if (plugin->funclist->FindCRL != NULL) {
		return (plugin->funclist->FindCRL(handle, numattr,
		    attrlist));
	}
	return (KMF_ERR_FUNCTION_NOT_FOUND);
}

KMF_RETURN
kmf_find_cert_in_crl(KMF_HANDLE_T handle, int numattr, KMF_ATTRIBUTE *attrlist)
{
	KMF_RETURN ret;
	KMF_PLUGIN *plugin;

	ret = setup_crl_call(handle, numattr, attrlist, &plugin);
	if (ret != KMF_OK)
		return (ret);

	if (plugin == NULL)
		return (KMF_ERR_PLUGIN_NOTFOUND);
	else if (plugin->funclist->FindCertInCRL != NULL)
		return (plugin->funclist->FindCertInCRL(handle, numattr,
		    attrlist));

	return (KMF_ERR_FUNCTION_NOT_FOUND);
}

KMF_RETURN
kmf_verify_crl_file(KMF_HANDLE_T handle, char *crlfile, KMF_DATA *tacert)
{
	KMF_PLUGIN *plugin;
	KMF_RETURN (*verifyCRLFile)(KMF_HANDLE_T, char *, KMF_DATA *);

	if (handle == NULL)
		return (KMF_ERR_BAD_PARAMETER);

	plugin = FindPlugin(handle, KMF_KEYSTORE_OPENSSL);
	if (plugin == NULL || plugin->dldesc == NULL) {
		return (KMF_ERR_PLUGIN_NOTFOUND);
	}

	verifyCRLFile = (KMF_RETURN(*)())dlsym(plugin->dldesc,
	    "OpenSSL_VerifyCRLFile");

	if (verifyCRLFile == NULL) {
		return (KMF_ERR_FUNCTION_NOT_FOUND);
	}

	return (verifyCRLFile(handle, crlfile, tacert));
}

KMF_RETURN
kmf_check_crl_date(KMF_HANDLE_T handle, char *crlname)
{
	KMF_PLUGIN *plugin;
	KMF_RETURN (*checkCRLDate)(void *, char *);
	KMF_RETURN ret = KMF_OK;

	if (handle == NULL)
		return (KMF_ERR_BAD_PARAMETER);

	CLEAR_ERROR(handle, ret);
	if (ret != KMF_OK)
		return (ret);

	plugin = FindPlugin(handle, KMF_KEYSTORE_OPENSSL);
	if (plugin == NULL || plugin->dldesc == NULL) {
		return (KMF_ERR_PLUGIN_NOTFOUND);
	}

	checkCRLDate = (KMF_RETURN(*)())dlsym(plugin->dldesc,
	    "OpenSSL_CheckCRLDate");

	if (checkCRLDate == NULL) {
		return (KMF_ERR_FUNCTION_NOT_FOUND);
	}

	return (checkCRLDate(handle, crlname));
}

KMF_RETURN
kmf_is_crl_file(KMF_HANDLE_T handle, char *filename, KMF_ENCODE_FORMAT *pformat)
{
	KMF_PLUGIN *plugin;
	KMF_RETURN (*IsCRLFileFn)(void *, char *, KMF_ENCODE_FORMAT *);
	KMF_RETURN ret = KMF_OK;

	CLEAR_ERROR(handle, ret);
	if (ret != KMF_OK)
		return (ret);

	if (filename  == NULL || pformat == NULL) {
		return (KMF_ERR_BAD_PARAMETER);
	}

	/*
	 * This framework function is actually implemented in the openssl
	 * plugin library, so we find the function address and call it.
	 */
	plugin = FindPlugin(handle, KMF_KEYSTORE_OPENSSL);
	if (plugin == NULL || plugin->dldesc == NULL) {
		return (KMF_ERR_PLUGIN_NOTFOUND);
	}

	IsCRLFileFn = (KMF_RETURN(*)())dlsym(plugin->dldesc,
	    "OpenSSL_IsCRLFile");
	if (IsCRLFileFn == NULL) {
		return (KMF_ERR_FUNCTION_NOT_FOUND);
	}

	return (IsCRLFileFn(handle, filename, pformat));
}
