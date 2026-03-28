#include "CsrGen.h"
#include "mbedtls/pk.h"
#include "mbedtls/ecp.h"
#include "mbedtls/ctr_drbg.h"
#include "mbedtls/entropy.h"
#include "mbedtls/x509_csr.h"

bool genEcKeyAndCsrPem(const String& commonName, String& outPrivKeyPem, String& outCsrPem) {
  mbedtls_pk_context key;
  mbedtls_x509write_csr req;
  mbedtls_ctr_drbg_context ctr_drbg;
  mbedtls_entropy_context entropy;

  // Move this up so goto can't skip its initialization
  String subj = "CN=" + commonName;

  mbedtls_pk_init(&key);
  mbedtls_x509write_csr_init(&req);
  mbedtls_ctr_drbg_init(&ctr_drbg);
  mbedtls_entropy_init(&entropy);

  const char* pers = "esp32-csr";
  int rc = mbedtls_ctr_drbg_seed(&ctr_drbg, mbedtls_entropy_func, &entropy,
                                 (const unsigned char*)pers, strlen(pers));
  if (rc != 0) goto fail;

  rc = mbedtls_pk_setup(&key, mbedtls_pk_info_from_type(MBEDTLS_PK_ECKEY));
  if (rc != 0) goto fail;

  rc = mbedtls_ecp_gen_key(MBEDTLS_ECP_DP_SECP256R1,
                           mbedtls_pk_ec(key),
                           mbedtls_ctr_drbg_random, &ctr_drbg);
  if (rc != 0) goto fail;

  // Private key PEM
  {
    unsigned char buf[2048];
    memset(buf, 0, sizeof(buf));
    rc = mbedtls_pk_write_key_pem(&key, buf, sizeof(buf));
    if (rc != 0) goto fail;
    outPrivKeyPem = String((char*)buf);
  }

  // CSR PEM
  mbedtls_x509write_csr_set_md_alg(&req, MBEDTLS_MD_SHA256);
  mbedtls_x509write_csr_set_key(&req, &key);

  rc = mbedtls_x509write_csr_set_subject_name(&req, subj.c_str());
  if (rc != 0) goto fail;

  {
    unsigned char buf[2048];
    memset(buf, 0, sizeof(buf));
    rc = mbedtls_x509write_csr_pem(&req, buf, sizeof(buf),
                                  mbedtls_ctr_drbg_random, &ctr_drbg);
    if (rc != 0) goto fail;
    outCsrPem = String((char*)buf);
  }

  mbedtls_x509write_csr_free(&req);
  mbedtls_pk_free(&key);
  mbedtls_ctr_drbg_free(&ctr_drbg);
  mbedtls_entropy_free(&entropy);
  return true;

fail:
  mbedtls_x509write_csr_free(&req);
  mbedtls_pk_free(&key);
  mbedtls_ctr_drbg_free(&ctr_drbg);
  mbedtls_entropy_free(&entropy);
  return false;
}