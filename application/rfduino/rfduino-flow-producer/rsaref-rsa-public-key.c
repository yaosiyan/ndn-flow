/* -*- Mode:C++; c-file-style:"gnu"; indent-tabs-mode:nil -*- */
/**
 * Copyright (C) 2016 Regents of the University of California.
 * @author: Jeff Thompson <jefft0@remap.ucla.edu>
 * 
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU Lesser General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version, with the additional exemption that
 * compiling, linking, and/or using OpenSSL is allowed.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
 * A copy of the GNU Lesser General Public License is in the file COPYING.
 */

#if 0 // TODO: Move header files to the proper location.
#include "../../../contrib/rsaref/source/global.h"
#include "../../../contrib/rsaref/source/rsaref.h"
#include "../../../contrib/rsaref/source/rsa.h"
#else
#include <rsaref/source/global.h>
#include <rsaref/source/rsaref.h>
#include <rsaref/source/rsa.h>
#endif
#include "rsaref-rsa-public-key.h"

R_RANDOM_STRUCT *ndn_RsarefCrypto_getGlobalRandomStruct();

ndn_Error
ndn_RsarefRsaPublicKey_encrypt
  (const struct ndn_RsarefRsaPublicKey *self, const uint8_t *plainData,
   size_t plainDataLength, ndn_EncryptAlgorithmType algorithmType,
   uint8_t *encryptedData, size_t *encryptedDataLength)
{
  unsigned int outputLength, bytesNeeded;

  // RSAPublicEncrypt doesn't check this, so do it here.
  R_GetRandomBytesNeeded(&bytesNeeded, ndn_RsarefCrypto_getGlobalRandomStruct());
  if (bytesNeeded > 0)
    return NDN_ERROR_Error_in_encrypt_operation;

  if (algorithmType != ndn_EncryptAlgorithmType_RsaPkcs)
    return NDN_ERROR_Unsupported_algorithm_type;

  if (RSAPublicEncrypt
      ((unsigned char *)encryptedData, &outputLength, (unsigned char *)plainData,
       plainDataLength, self->publicKey, ndn_RsarefCrypto_getGlobalRandomStruct()) != 0)
    return NDN_ERROR_Error_in_encrypt_operation;

  *encryptedDataLength = outputLength;
  return NDN_ERROR_success;
}

