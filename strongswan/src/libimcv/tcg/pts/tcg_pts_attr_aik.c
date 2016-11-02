/*
 * Copyright (C) 2011-2012 Sansar Choinyambuu
 * Copyright (C) 2011-2014 Andreas Steffen
 * HSR Hochschule fuer Technik Rapperswil
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License as published by the
 * Free Software Foundation; either version 2 of the License, or (at your
 * option) any later version.  See <http://www.fsf.org/copyleft/gpl.txt>.
 *
 * This program is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY
 * or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License
 * for more details.
 */

#include "tcg_pts_attr_aik.h"

#include <pa_tnc/pa_tnc_msg.h>
#include <bio/bio_writer.h>
#include <bio/bio_reader.h>
#include <utils/debug.h>

typedef struct private_tcg_pts_attr_aik_t private_tcg_pts_attr_aik_t;

/**
 * Attestation Identity Key
 * see section 3.13 of PTS Protocol: Binding to TNC IF-M Specification
 *
 *					   1				   2				   3
 *   0 1 2 3 4 5 6 7 8 9 0 1 2 3 4 5 6 7 8 9 0 1 2 3 4 5 6 7 8 9 0 1
 *  +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
 *  |	 Flags	    |	Attestation Identity Key (Variable Length)  ~
 *  +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
 *  |		   Attestation Identity Key (Variable Length)		    ~
 *  +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
 */

#define PTS_AIK_SIZE				4
#define PTS_AIK_FLAGS_NONE			0
#define PTS_AIK_FLAGS_NAKED_KEY		(1<<7)
/**
 * Private data of an tcg_pts_attr_aik_t object.
 */
struct private_tcg_pts_attr_aik_t {

	/**
	 * Public members of tcg_pts_attr_aik_t
	 */
	tcg_pts_attr_aik_t public;

	/**
	 * Vendor-specific attribute type
	 */
	pen_type_t type;

	/**
	 * Length of attribute value
	 */
	size_t length;

	/**
	 * Attribute value or segment
	 */
	chunk_t value;

	/**
	 * Noskip flag
	 */
	bool noskip_flag;

	/**
	 * AIK Certificate or Public Key
	 */
	certificate_t *aik;

	/**
	 * Reference count
	 */
	refcount_t ref;
};

METHOD(pa_tnc_attr_t, get_type, pen_type_t,
	private_tcg_pts_attr_aik_t *this)
{
	return this->type;
}

METHOD(pa_tnc_attr_t, get_value, chunk_t,
	private_tcg_pts_attr_aik_t *this)
{
	return this->value;
}

METHOD(pa_tnc_attr_t, get_noskip_flag, bool,
	private_tcg_pts_attr_aik_t *this)
{
	return this->noskip_flag;
}

METHOD(pa_tnc_attr_t, set_noskip_flag,void,
	private_tcg_pts_attr_aik_t *this, bool noskip)
{
	this->noskip_flag = noskip;
}

METHOD(pa_tnc_attr_t, build, void,
	private_tcg_pts_attr_aik_t *this)
{
	bio_writer_t *writer;
	uint8_t flags = PTS_AIK_FLAGS_NONE;
	cred_encoding_type_t encoding_type = CERT_ASN1_DER;
	chunk_t aik_blob;

	if (this->value.ptr)
	{
		return;
	}
	if (this->aik->get_type(this->aik) == CERT_TRUSTED_PUBKEY)
	{
		flags |= PTS_AIK_FLAGS_NAKED_KEY;
		encoding_type = PUBKEY_SPKI_ASN1_DER;
	}
	if (!this->aik->get_encoding(this->aik, encoding_type, &aik_blob))
	{
		DBG1(DBG_TNC, "encoding of Attestation Identity Key failed");
		aik_blob = chunk_empty;
	}
	writer = bio_writer_create(PTS_AIK_SIZE);
	writer->write_uint8(writer, flags);
	writer->write_data (writer, aik_blob);
	this->value = writer->extract_buf(writer);
	this->length = this->value.len;
	writer->destroy(writer);
	free(aik_blob.ptr);
}

METHOD(pa_tnc_attr_t, process, status_t,
	private_tcg_pts_attr_aik_t *this, uint32_t *offset)
{
	bio_reader_t *reader;
	uint8_t flags;
	certificate_type_t type;
	chunk_t aik_blob;

	*offset = 0;

	if (this->value.len < this->length)
	{
		return NEED_MORE;
	}
	if (this->value.len < PTS_AIK_SIZE)
	{
		DBG1(DBG_TNC, "insufficient data for Attestation Identity Key");
		return FAILED;
	}
	reader = bio_reader_create(this->value);
	reader->read_uint8(reader, &flags);
	reader->read_data (reader, reader->remaining(reader), &aik_blob);

	type = (flags & PTS_AIK_FLAGS_NAKED_KEY) ? CERT_TRUSTED_PUBKEY : CERT_X509;

	this->aik = lib->creds->create(lib->creds, CRED_CERTIFICATE, type,
								   BUILD_BLOB_PEM, aik_blob, BUILD_END);
	reader->destroy(reader);

	if (!this->aik)
	{
		DBG1(DBG_TNC, "parsing of Attestation Identity Key failed");
		*offset = 0;
		return FAILED;
	}
	return SUCCESS;
}

METHOD(pa_tnc_attr_t, add_segment, void,
	private_tcg_pts_attr_aik_t *this, chunk_t segment)
{
	this->value = chunk_cat("mc", this->value, segment);
}

METHOD(pa_tnc_attr_t, get_ref, pa_tnc_attr_t*,
	private_tcg_pts_attr_aik_t *this)
{
	ref_get(&this->ref);
	return &this->public.pa_tnc_attribute;
}

METHOD(pa_tnc_attr_t, destroy, void,
	private_tcg_pts_attr_aik_t *this)
{
	if (ref_put(&this->ref))
	{
		DESTROY_IF(this->aik);
		free(this->value.ptr);
		free(this);
	}
}

METHOD(tcg_pts_attr_aik_t, get_aik, certificate_t*,
	private_tcg_pts_attr_aik_t *this)
{
	return this->aik;
}

/**
 * Described in header.
 */
pa_tnc_attr_t *tcg_pts_attr_aik_create(certificate_t *aik)
{
	private_tcg_pts_attr_aik_t *this;

	INIT(this,
		.public = {
			.pa_tnc_attribute = {
				.get_type = _get_type,
				.get_value = _get_value,
				.get_noskip_flag = _get_noskip_flag,
				.set_noskip_flag = _set_noskip_flag,
				.build = _build,
				.process = _process,
				.add_segment = _add_segment,
				.get_ref = _get_ref,
				.destroy = _destroy,
			},
			.get_aik = _get_aik,
		},
		.type = { PEN_TCG, TCG_PTS_AIK },
		.aik = aik->get_ref(aik),
		.ref = 1,
	);

	return &this->public.pa_tnc_attribute;
}


/**
 * Described in header.
 */
pa_tnc_attr_t *tcg_pts_attr_aik_create_from_data(size_t length, chunk_t data)
{
	private_tcg_pts_attr_aik_t *this;

	INIT(this,
		.public = {
			.pa_tnc_attribute = {
				.get_type = _get_type,
				.get_value = _get_value,
				.get_noskip_flag = _get_noskip_flag,
				.set_noskip_flag = _set_noskip_flag,
				.build = _build,
				.process = _process,
				.add_segment = _add_segment,
				.get_ref = _get_ref,
				.destroy = _destroy,
			},
			.get_aik = _get_aik,
		},
		.type = { PEN_TCG, TCG_PTS_AIK },
		.length = length,
		.value = chunk_clone(data),
		.ref = 1,
	);

	return &this->public.pa_tnc_attribute;
}
