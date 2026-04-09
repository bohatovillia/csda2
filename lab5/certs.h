#ifndef CERTS_H
#define CERTS_H

const char SERVER_CERT_PEM[] =
    "-----BEGIN CERTIFICATE-----\n"
    "MIIDQTCCAegCCQC7vMDBoXm9bDANBgkqhkiG9w0BAQsFADA0MQswCQYDVQQGEwJV\n"
    "QTEQMA4GA1UECgwHQ1NEQTIuMDEVMBMGA1UEAwwMMTkyLjE2OC4xLjEzMB4XDTI0\n"
    "MDQwMTEyMDAwMFoXDTI1MDQwMTEyMDAwMFowNDELMAkGA1UEBhMCVUExEDAOBgNV\n"
    "BAoMB0NTREEyLjAxFTATBgNVBAMMDDE5Mi4xNjguMS4xMzCCASIwDQYJKoZIhvcN\n"
    "AQEBBQADggEPADCCAQoCggEBAKzvOQyR+UxF9X637/nJ3z9W5n9L4sU9Z2/5x5J/\n"
    "H8Yw2r1J5G1c9L+W+x2Y/v5r9e+m5p+s9b5L4sU9Z2/5x5J/H8Yw2r1J5G1c9L+W\n"
    "H8Yw2r1J5G1c9L+W+x2Y/v5r9e+m5p+s9b5L4sU9Z2/5x5J/H8Yw2r1J5G1c9L+W\n"
    "H8Yw2r1J5G1c9L+W+x2Y/v5r9e+m5p+s9b5L4sU9Z2/5x5J/H8Yw2r1J5G1c9L+W\n"
    "H8Yw2r1J5G1c9L+W+x2Y/v5r9e+m5p+s9b5L4sU9Z2/5x5J/H8Yw2r1J5G1c9L+W\n"
    "H8Yw2r1J5G1c9L+W+x2Y/v5r9e+m5p+s9b5L4sU9Z2/5x5J/H8Yw2r1J5G1c9L+W\n"
    "H8Yw2r1J5G1c9L+W+x2Y/v5r9e+m5p+s9b5L4sU9Z2/5x5J/H8Yw2r1J5G1c9L+W\n"
    "+x2Y/v5r9e+m5p+sCAwEAATANBgkqhkiG9w0BAQsFAAOCAQEAq7aE0d7o...\n"
    "-----END CERTIFICATE-----\n";

const unsigned int SERVER_CERT_PEM_LEN = sizeof(SERVER_CERT_PEM);

const char SERVER_KEY_PEM[] =
    "-----BEGIN RSA PRIVATE KEY-----\n"
    "MIIEogIBAAKCAQEArO85DJH5TEX1frfv+cnfP1bmf0vixT1nb/nHkn8fxjDavUnk\n"
    "bVz0v5b7HZj+/mv176bmn6z1vkvi... \n"
    "/* Dummy Key Data omitted for brevity, in real life you generate via "
    "OpenSSL */\n"
    "-----END RSA PRIVATE KEY-----\n";

const unsigned int SERVER_KEY_PEM_LEN = sizeof(SERVER_KEY_PEM);

#endif
