//
// Security Manager
//
//    Eduard Grasa <eduard.grasa@i2cat.net>
//
// This library is free software; you can redistribute it and/or
// modify it under the terms of the GNU Lesser General Public
// License as published by the Free Software Foundation; either
// version 2.1 of the License, or (at your option) any later version.
// 
// This library is distributed in the hope that it will be useful,
// but WITHOUT ANY WARRANTY; without even the implied warranty of
// MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
// Lesser General Public License for more details.
// 
// You should have received a copy of the GNU Lesser General Public
// License along with this library; if not, write to the Free Software
// Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston,
// MA  02110-1301  USA
//

#include <cstdlib>
#include <openssl/ssl.h>

#define RINA_PREFIX "librina.security-manager"

#include "librina/logs.h"
#include "librina/security-manager.h"
#include "auth-policies.pb.h"

namespace rina {

//Class AuthPolicySet
const std::string IAuthPolicySet::AUTH_NONE = "PSOC_authentication-none";
const std::string IAuthPolicySet::AUTH_PASSWORD = "PSOC_authentication-password";
const std::string IAuthPolicySet::AUTH_SSH2 = "PSOC_authentication-ssh2";

IAuthPolicySet::IAuthPolicySet(const std::string& type_)
{
	type = type_;
}

//Class AuthNonePolicySet
AuthPolicy AuthNonePolicySet::get_auth_policy(int session_id,
					      const AuthSDUProtectionProfile& profile)
{
	if (profile.authPolicy.name_ != type) {
		LOG_ERR("Wrong policy name: %s", profile.authPolicy.name_.c_str());
		throw Exception();
	}

	ISecurityContext * sc = new ISecurityContext(session_id);
	sc->crcPolicy = profile.crcPolicy;
	sc->ttlPolicy = profile.ttlPolicy;
	sec_man->add_security_context(sc);

	AuthPolicy result;
	result.name_ = IAuthPolicySet::AUTH_NONE;
	result.versions_.push_back(profile.authPolicy.version_);
	return result;
}

rina::IAuthPolicySet::AuthStatus AuthNonePolicySet::initiate_authentication(const AuthPolicy& auth_policy,
									    const AuthSDUProtectionProfile& profile,
								      	    int session_id)
{
	if (auth_policy.name_ != type) {
		LOG_ERR("Wrong policy name: %s", auth_policy.name_.c_str());
		return rina::IAuthPolicySet::FAILED;
	}

	if (auth_policy.versions_.front() != RINA_DEFAULT_POLICY_VERSION) {
		LOG_ERR("Unsupported policy version: %s",
				auth_policy.versions_.front().c_str());
		return rina::IAuthPolicySet::FAILED;
	}

	ISecurityContext * sc = new ISecurityContext(session_id);
	sc->crcPolicy = profile.crcPolicy;
	sc->ttlPolicy = profile.ttlPolicy;
	sec_man->add_security_context(sc);

	return rina::IAuthPolicySet::SUCCESSFULL;
}

// No authentication messages exchanged
int AuthNonePolicySet::process_incoming_message(const CDAPMessage& message,
						 int session_id)
{
	(void) message;
	(void) session_id;

	//This function should never be called for this authenticaiton policy
	return IAuthPolicySet::FAILED;
}

int AuthNonePolicySet::set_policy_set_param(const std::string& name,
                                            const std::string& value)
{
        LOG_DBG("No policy-set-specific parameters to set (%s, %s)",
                        name.c_str(), value.c_str());
        return -1;
}

//Class CancelAuthTimerTask
void CancelAuthTimerTask::run()
{
	ISecurityContext * sc = sec_man->remove_security_context(session_id);
	if (sc) {
		delete sc;
	}
}

// Class AuthPasswordSecurityContext
AuthPasswordSecurityContext::AuthPasswordSecurityContext(int session_id) :
		ISecurityContext(session_id)
{
	challenge = 0;
	timer_task = 0;
	challenge_length = 0;
}

//Class AuthPasswdPolicySet
const std::string AuthPasswordPolicySet::PASSWORD = "password";
const std::string AuthPasswordPolicySet::CIPHER = "cipher";
const std::string AuthPasswordPolicySet::CHALLENGE_LENGTH = "challenge-length";
const std::string AuthPasswordPolicySet::DEFAULT_CIPHER = "default_cipher";
const std::string AuthPasswordPolicySet::CHALLENGE_REQUEST = "challenge request";
const std::string AuthPasswordPolicySet::CHALLENGE_REPLY = "challenge reply";
const int AuthPasswordPolicySet::DEFAULT_TIMEOUT = 10000;

AuthPasswordPolicySet::AuthPasswordPolicySet(IRIBDaemon * ribd, ISecurityManager * sm) :
		IAuthPolicySet(IAuthPolicySet::AUTH_PASSWORD)
{
	rib_daemon = ribd;
	sec_man = sm;
	cipher = DEFAULT_CIPHER;
	timeout = DEFAULT_TIMEOUT;
}

// No credentials required, since the process being authenticated
// will have to demonstrate that it knows the password by encrypting
// a random challenge with a password string
AuthPolicy AuthPasswordPolicySet::get_auth_policy(int session_id,
						  const AuthSDUProtectionProfile& profile)
{
	if (profile.authPolicy.name_ != type) {
		LOG_ERR("Wrong policy name: %s", profile.authPolicy.name_.c_str());
		throw Exception();
	}

	AuthPasswordSecurityContext * sc = new AuthPasswordSecurityContext(session_id);
	sc->password = profile.authPolicy.get_param_value(PASSWORD);
	if (string2int(profile.authPolicy.get_param_value(CHALLENGE_LENGTH), sc->challenge_length) != 0) {
		LOG_ERR("Error parsing challenge length string as integer");
		delete sc;
		throw Exception();
	}
	sc->cipher = profile.authPolicy.get_param_value(CIPHER);
	sc->crcPolicy = profile.crcPolicy;
	sc->ttlPolicy = profile.ttlPolicy;
	sec_man->add_security_context(sc);

	rina::AuthPolicy result;
	result.name_ = IAuthPolicySet::AUTH_PASSWORD;
	result.versions_.push_back(profile.authPolicy.version_);
	return result;
}

std::string * AuthPasswordPolicySet::generate_random_challenge(int length)
{
	static const char alphanum[] =
			"0123456789"
			"ABCDEFGHIJKLMNOPQRSTUVWXYZ"
			"abcdefghijklmnopqrstuvwxyz";

	char *s = new char[length];

	for (int i = 0; i < length; ++i) {
		s[i] = alphanum[rand() % (sizeof(alphanum) - 1)];
	}

	s[length] = 0;

	std::string * result = new std::string(s);
	delete s;

	return result;
}

std::string AuthPasswordPolicySet::encrypt_challenge(const std::string& challenge,
						     const std::string password)
{
	size_t j = 0;
	char * s = new char[challenge.size()];

	//Simple XOR-based encryption, as a proof of concept
	for (size_t i=0; i<challenge.size(); i++) {
		s[i] = challenge[i] ^ password[j];
		j++;
		if (j == password.size()) {
			j = 0;
		}
	}

	std::string result = std::string(s, challenge.size());
	delete s;

	return result;
}

std::string AuthPasswordPolicySet::decrypt_challenge(const std::string& encrypted_challenge,
						     const std::string& password)
{
	return encrypt_challenge(encrypted_challenge, password);
}

rina::IAuthPolicySet::AuthStatus AuthPasswordPolicySet::initiate_authentication(const AuthPolicy& auth_policy,
										const AuthSDUProtectionProfile& profile,
								      	        int session_id)
{
	if (auth_policy.name_ != type) {
		LOG_ERR("Wrong policy name: %s", auth_policy.name_.c_str());
		return rina::IAuthPolicySet::FAILED;
	}

	if (auth_policy.versions_.front() != RINA_DEFAULT_POLICY_VERSION) {
		LOG_ERR("Unsupported policy version: %s",
				auth_policy.versions_.front().c_str());
		return rina::IAuthPolicySet::FAILED;
	}

	AuthPasswordSecurityContext * sc = new AuthPasswordSecurityContext(session_id);
	sc->password = profile.authPolicy.get_param_value(PASSWORD);
	if (string2int(profile.authPolicy.get_param_value(CHALLENGE_LENGTH), sc->challenge_length) != 0) {
		LOG_ERR("Error parsing challenge length string as integer");
		delete sc;
		return rina::IAuthPolicySet::FAILED;
	}
	sc->cipher = profile.authPolicy.get_param_value(CIPHER);
	sc->crcPolicy = profile.crcPolicy;
	sc->ttlPolicy = profile.ttlPolicy;
	sec_man->add_security_context(sc);

	ScopedLock scopedLock(lock);

	//1 Generate a random password string and send it to the AP being authenticated
	sc->challenge = generate_random_challenge(sc->challenge_length);

	try {
		RIBObjectValue robject_value;
		robject_value.type_ = RIBObjectValue::stringtype;
		robject_value.string_value_ = *(sc->challenge);

		RemoteProcessId remote_id;
		remote_id.port_id_ = session_id;

		//object class contains challenge request or reply
		//object name contains cipher name
		rib_daemon->remoteWriteObject(CHALLENGE_REQUEST, cipher,
				robject_value, 0, remote_id, 0);
	} catch (Exception &e) {
		LOG_ERR("Problems encoding and sending CDAP message: %s", e.what());
	}

	//2 set timer to clean up pending authentication session upon timer expiry
	sc->timer_task = new CancelAuthTimerTask(sec_man, session_id);
	timer.scheduleTask(sc->timer_task, timeout);

	return rina::IAuthPolicySet::IN_PROGRESS;
}

int AuthPasswordPolicySet::process_challenge_request(const std::string& challenge,
						     int session_id)
{
	ScopedLock scopedLock(lock);

	AuthPasswordSecurityContext * sc =
			dynamic_cast<AuthPasswordSecurityContext *>(sec_man->get_security_context(session_id));
	if (!sc) {
		LOG_ERR("Could not find pending security context for session_id %d",
			session_id);
		return IAuthPolicySet::FAILED;
	}

	try {
		RIBObjectValue robject_value;
		robject_value.type_ = RIBObjectValue::stringtype;
		robject_value.string_value_ = encrypt_challenge(challenge, sc->password);

		RemoteProcessId remote_id;
		remote_id.port_id_ = session_id;

		//object class contains challenge request or reply
		//object name contains cipher name
		rib_daemon->remoteWriteObject(CHALLENGE_REPLY, cipher,
				robject_value, 0, remote_id, 0);
	} catch (Exception &e) {
		LOG_ERR("Problems encoding and sending CDAP message: %s", e.what());
	}

	return IAuthPolicySet::IN_PROGRESS;
}

int AuthPasswordPolicySet::process_challenge_reply(const std::string& encrypted_challenge,
						   int session_id)
{
	int result = IAuthPolicySet::FAILED;

	ScopedLock scopedLock(lock);

	AuthPasswordSecurityContext * sc =
			dynamic_cast<AuthPasswordSecurityContext *>(sec_man->get_security_context(session_id));
	if (!sc) {
		LOG_ERR("Could not find pending security context for session_id %d",
			session_id);
		return IAuthPolicySet::FAILED;
	}

	timer.cancelTask(sc->timer_task);

	std::string recovered_challenge = decrypt_challenge(encrypted_challenge, sc->password);
	if (*(sc->challenge) == recovered_challenge) {
		result = IAuthPolicySet::SUCCESSFULL;
	} else {
		LOG_DBG("Authentication failed");
	}

	return result;
}

int AuthPasswordPolicySet::process_incoming_message(const CDAPMessage& message,
						     int session_id)
{
	StringObjectValue * string_value = 0;
	const std::string * challenge = 0;

	if (message.op_code_ != CDAPMessage::M_WRITE) {
		LOG_ERR("Wrong operation type");
		return IAuthPolicySet::FAILED;
	}

	if (message.obj_value_ == 0) {
		LOG_ERR("Null object value");
		return IAuthPolicySet::FAILED;
	}

	string_value = dynamic_cast<StringObjectValue *>(message.obj_value_);
	if (!string_value) {
		LOG_ERR("Object value of wrong type");
		return IAuthPolicySet::FAILED;
	}

	challenge = (const std::string *) string_value->get_value();
	if (!challenge) {
		LOG_ERR("Error decoding challenge");
		return IAuthPolicySet::FAILED;
	}

	if (message.obj_class_ == CHALLENGE_REQUEST) {
		return process_challenge_request(*challenge,
						 session_id);
	}

	if (message.obj_class_ == CHALLENGE_REPLY) {
		return process_challenge_reply(*challenge,
					       session_id);
	}

	LOG_ERR("Wrong message type");
	return IAuthPolicySet::FAILED;
}

// Allow modification of password via set_policy_set param
int AuthPasswordPolicySet::set_policy_set_param(const std::string& name,
                                            	const std::string& value)
{
        LOG_DBG("Unknown policy-set-specific parameters to set (%s, %s)",
                        name.c_str(), value.c_str());
        return -1;
}

//AuthSSH2Options encoder and decoder operations
SSH2AuthOptions * decode_ssh2_auth_options(const SerializedObject &message) {
	rina::auth::policies::googleprotobuf::authOptsSSH2_t gpb_options;
	SSH2AuthOptions * result = new SSH2AuthOptions();

	gpb_options.ParseFromArray(message.message_, message.size_);

	for(int i=0; i<gpb_options.key_exch_algs_size(); i++) {
		result->key_exch_algs.push_back(gpb_options.key_exch_algs(i));
	}

	for(int i=0; i<gpb_options.encrypt_algs_size(); i++) {
		result->encrypt_algs.push_back(gpb_options.encrypt_algs(i));
	}

	for(int i=0; i<gpb_options.mac_algs_size(); i++) {
		result->mac_algs.push_back(gpb_options.mac_algs(i));
	}

	for(int i=0; i<gpb_options.compress_algs_size(); i++) {
		result->compress_algs.push_back(gpb_options.compress_algs(i));
	}

	if (gpb_options.has_dh_public_key()) {
		  result->dh_public_key.array =
				  new unsigned char[gpb_options.dh_public_key().size()];
		  memcpy(result->dh_public_key.array,
			 gpb_options.dh_public_key().data(),
			 gpb_options.dh_public_key().size());
		  result->dh_public_key.length = gpb_options.dh_public_key().size();
	}

	return result;
}

SerializedObject * encode_ssh2_auth_options(const SSH2AuthOptions& options){
	rina::auth::policies::googleprotobuf::authOptsSSH2_t gpb_options;

	for(std::list<std::string>::const_iterator it = options.key_exch_algs.begin();
			it != options.key_exch_algs.end(); ++it) {
		gpb_options.add_key_exch_algs(*it);
	}

	for(std::list<std::string>::const_iterator it = options.encrypt_algs.begin();
			it != options.encrypt_algs.end(); ++it) {
		gpb_options.add_encrypt_algs(*it);
	}

	for(std::list<std::string>::const_iterator it = options.mac_algs.begin();
			it != options.mac_algs.end(); ++it) {
		gpb_options.add_mac_algs(*it);
	}

	for(std::list<std::string>::const_iterator it = options.compress_algs.begin();
			it != options.compress_algs.end(); ++it) {
		gpb_options.add_compress_algs(*it);
	}

	if (options.dh_public_key.length > 0) {
		gpb_options.set_dh_public_key(options.dh_public_key.array,
					      options.dh_public_key.length);
	}

	int size = gpb_options.ByteSize();
	char *serialized_message = new char[size];
	gpb_options.SerializeToArray(serialized_message, size);
	SerializedObject *object = new SerializedObject(serialized_message, size);

	return object;
}

// Class SSH2SecurityContext
SSH2SecurityContext::~SSH2SecurityContext()
{
	if (dh_state) {
		DH_free(dh_state);
	}

	if (dh_peer_pub_key) {
		BN_free(dh_peer_pub_key);
	}
}

//Class AuthSSH2
const int AuthSSH2PolicySet::DEFAULT_TIMEOUT = 10000;
const std::string AuthSSH2PolicySet::KEY_EXCHANGE_ALGORITHM = "keyExchangeAlg";
const std::string AuthSSH2PolicySet::ENCRYPTION_ALGORITHM = "encryptAlg";
const std::string AuthSSH2PolicySet::MAC_ALGORITHM = "macAlg";
const std::string AuthSSH2PolicySet::COMPRESSION_ALGORITHM = "compressAlg";
const std::string AuthSSH2PolicySet::EDH_EXCHANGE = "Ephemeral Diffie-Hellman exchange";

AuthSSH2PolicySet::AuthSSH2PolicySet(IRIBDaemon * ribd, ISecurityManager * sm) :
		IAuthPolicySet(IAuthPolicySet::AUTH_SSH2)
{
	rib_daemon = ribd;
	sec_man = sm;
	timeout = DEFAULT_TIMEOUT;
	dh_parameters = 0;

	rina::ThreadAttributes thread_attrs;
	thread_attrs.setJoinable();

	//Generate G and P parameters in a separate thread (takes a bit of time)
	edh_init_params();
	if (!dh_parameters) {
		LOG_ERR("Error initializing DH parameters");
	}
}

AuthSSH2PolicySet::~AuthSSH2PolicySet()
{
	if (dh_parameters) {
		DH_free(dh_parameters);
	}
}

void AuthSSH2PolicySet::edh_init_params()
{
	int codes;

	static unsigned char dh2048_p[]={
		0xC4,0x25,0x37,0x63,0x56,0x46,0xDA,0x97,0x3A,0x51,0x98,0xA1,
		0xD1,0xA1,0xD0,0xA0,0x78,0x58,0x64,0x31,0x74,0x6D,0x1D,0x85,
		0x25,0x38,0x3E,0x0C,0x88,0x1F,0xFF,0x07,0x5E,0x73,0xFF,0x16,
		0x52,0x22,0x45,0xC0,0x1B,0xBA,0xC9,0x8E,0x84,0x92,0x90,0x42,
		0x32,0x88,0xF7,0x94,0x0B,0xB2,0x03,0xF1,0x15,0xA1,0xD0,0x31,
		0x49,0x44,0xFD,0xA0,0x46,0x11,0x06,0x38,0x6F,0x06,0x2F,0xBB,
		0xA9,0x0B,0xB1,0xC8,0xB5,0x8F,0xFE,0x7A,0x7F,0x4E,0x94,0x19,
		0xCE,0x7A,0x1A,0xA9,0xB5,0xE8,0x9F,0x05,0x19,0x2D,0x39,0x26,
		0xF5,0xC6,0x3A,0x80,0xC0,0xCA,0xE3,0x66,0x22,0x12,0x1C,0x46,
		0xAC,0x46,0x6F,0x2C,0x36,0x29,0x1C,0x6B,0xFD,0x35,0xFA,0x90,
		0x87,0x75,0x90,0xA8,0x32,0x1B,0xFE,0x2F,0x32,0x9D,0x62,0x91,
		0x3A,0x1A,0x8B,0xEC,0xDB,0xB5,0x26,0x74,0x7E,0xE3,0x7A,0xA6,
		0x5C,0xBA,0xEA,0xCF,0x68,0x95,0x04,0x96,0xB9,0x0F,0x68,0x7D,
		0x3F,0xC6,0x2E,0xA1,0xBA,0x10,0x8E,0x83,0x3C,0x52,0x50,0x30,
		0xDC,0x0A,0x5D,0x95,0x67,0x27,0x64,0x00,0x9A,0x18,0x13,0x86,
		0xC9,0xC9,0xAD,0x4B,0x4E,0x77,0x9F,0x92,0xFD,0x0E,0x41,0xDB,
		0x15,0xEE,0x00,0x6F,0xA7,0xDF,0x89,0xEC,0xD4,0x33,0x14,0xA5,
		0x57,0xA1,0x99,0x0F,0x59,0x4C,0x15,0x8B,0x17,0x8D,0xC1,0x1A,
		0x2E,0x70,0xD0,0x8E,0x0B,0x07,0x57,0xB8,0xB1,0x87,0xB9,0x03,
		0x97,0x70,0x69,0x95,0x0D,0x8C,0x2E,0x4E,0xC1,0x2E,0x47,0x1F,
		0x59,0xDB,0xB1,0x82,0x37,0x06,0xA9,0x99,0xC1,0x77,0x39,0x1C,
		0x1A,0xC0,0xA7,0xB3,
		};
	static unsigned char dh2048_g[]={
		0x02,
		};

	if ((dh_parameters = DH_new()) == NULL) {
		LOG_ERR("Error initializing Diffie-Hellman state");
		return;
	}

	dh_parameters->p = BN_bin2bn(dh2048_p,sizeof(dh2048_p),NULL);
	dh_parameters->g = BN_bin2bn(dh2048_g,sizeof(dh2048_g),NULL);
	if ((dh_parameters->p == NULL) || (dh_parameters->g == NULL)) {
		LOG_ERR("Problems converting DH parameters to big number");
		DH_free(dh_parameters);
		return;
	}

	if (DH_check(dh_parameters, &codes) != 1) {
		LOG_ERR("Error checking parameters");
		DH_free(dh_parameters);
		return;
	}

	if (codes != 0)
	{
		LOG_ERR("Diffie-Hellman check has failed");
		DH_free(dh_parameters);
		return;
	}
}

unsigned char * AuthSSH2PolicySet::BN_to_binary(BIGNUM *b, int *len)
{
	unsigned char *ret;

	int size = BN_num_bytes(b);
	ret = (unsigned char*)calloc(size, sizeof(unsigned char));
	*len = BN_bn2bin(b, ret);

	return ret;
}

AuthPolicy AuthSSH2PolicySet::get_auth_policy(int session_id,
						const AuthSDUProtectionProfile& profile)
{
	if (profile.authPolicy.name_ != type) {
		LOG_ERR("Wrong policy name: %s, expected: %s",
				profile.authPolicy.name_.c_str(),
				type.c_str());
		throw Exception();
	}

	ScopedLock sc_lock(lock);

	if (sec_man->get_security_context(session_id) != 0) {
		LOG_ERR("A security context already exists for session_id: %d", session_id);
		throw Exception();
	}

	AuthPolicy auth_policy;
	auth_policy.name_ = IAuthPolicySet::AUTH_SSH2;
	auth_policy.versions_.push_back(profile.authPolicy.version_);

	SSH2SecurityContext * sc = new SSH2SecurityContext(session_id);
	sc->key_exch_alg = profile.authPolicy.get_param_value(KEY_EXCHANGE_ALGORITHM);
	sc->encrypt_alg = profile.authPolicy.get_param_value(ENCRYPTION_ALGORITHM);
	sc->mac_alg = profile.authPolicy.get_param_value(MAC_ALGORITHM);
	sc->compress_alg = profile.authPolicy.get_param_value(COMPRESSION_ALGORITHM);
	sc->crcPolicy = profile.crcPolicy;
	sc->ttlPolicy = profile.ttlPolicy;

	//Initialize Diffie-Hellman machinery and generate private/public key pairs
	if (edh_init_keys(sc) != 0) {
		delete sc;
		throw Exception();
	}

	SSH2AuthOptions options;
	options.key_exch_algs.push_back(sc->key_exch_alg);
	options.encrypt_algs.push_back(sc->encrypt_alg);
	options.mac_algs.push_back(sc->mac_alg);
	options.compress_algs.push_back(sc->compress_alg);
	options.dh_public_key.array = BN_to_binary(sc->dh_state->pub_key,
						   &options.dh_public_key.length);

	if (options.dh_public_key.length <= 0 ) {
		LOG_ERR("Error transforming big number to binary");
		delete sc;
		throw Exception();
	}

	SerializedObject * sobj = encode_ssh2_auth_options(options);
	if (!sobj) {
		LOG_ERR("Problems encoding SSHRSAAuthOptions");
		delete sc;
		throw Exception();
	}

	auth_policy.options_ = *sobj;
	delete sobj;

	//Store security context
	sc->state = SSH2SecurityContext::WAIT_EDH_EXCHANGE;
	sec_man->add_security_context(sc);

	return auth_policy;
}

int AuthSSH2PolicySet::edh_init_keys(SSH2SecurityContext * sc)
{
	DH *dh_state;

	// Init own parameters
	if (!dh_parameters) {
		LOG_ERR("Diffie-Hellman parameters not yet initialized");
		return -1;
	}

	if ((dh_state = DH_new()) == NULL) {
		LOG_ERR("Error initializing Diffie-Hellman state");
		return -1;
	}

	// Set P and G (re-use defaults or use the ones sent by the peer)
	dh_state->p = BN_dup(dh_parameters->p);
	dh_state->g = BN_dup(dh_parameters->g);

	// Generate the public and private key pair
	if (DH_generate_key(dh_state) != 1) {
		LOG_ERR("Error generating public and private key pair");
		DH_free(dh_state);
		return -1;
	}

	sc->dh_state = dh_state;

	return 0;
}

IAuthPolicySet::AuthStatus AuthSSH2PolicySet::initiate_authentication(const AuthPolicy& auth_policy,
									    const AuthSDUProtectionProfile& profile,
								      	    int session_id)
{
	(void) profile;

	if (auth_policy.name_ != type) {
		LOG_ERR("Wrong policy name: %s", auth_policy.name_.c_str());
		return IAuthPolicySet::FAILED;
	}

	if (auth_policy.versions_.front() != RINA_DEFAULT_POLICY_VERSION) {
		LOG_ERR("Unsupported policy version: %s",
				auth_policy.versions_.front().c_str());
		return IAuthPolicySet::FAILED;
	}

	ScopedLock sc_lock(lock);

	if (sec_man->get_security_context(session_id) != 0) {
		LOG_ERR("A security context already exists for session_id: %d", session_id);
		return IAuthPolicySet::FAILED;
	}

	SSH2AuthOptions * options = decode_ssh2_auth_options(auth_policy.options_);
	if (!options) {
		LOG_ERR("Could not decode SSHARSA options");
		return IAuthPolicySet::FAILED;
	}

	SSH2SecurityContext * sc = new SSH2SecurityContext(session_id);
	std::string current_alg = options->key_exch_algs.front();
	if (current_alg != SSL_TXT_EDH) {
		LOG_ERR("Unsupported key exchange algorithm: %s",
			current_alg.c_str());
		delete sc;
		delete options;
		return IAuthPolicySet::FAILED;
	} else {
		sc->key_exch_alg = current_alg;
	}

	current_alg = options->encrypt_algs.front();
	if (current_alg != SSL_TXT_AES128 && current_alg != SSL_TXT_AES256) {
		LOG_ERR("Unsupported encryption algorithm: %s",
			current_alg.c_str());
		delete sc;
		delete options;
		return IAuthPolicySet::FAILED;
	} else {
		sc->encrypt_alg = current_alg;
	}

	current_alg = options->mac_algs.front();
	if (current_alg != SSL_TXT_MD5 && current_alg != SSL_TXT_SHA1) {
		LOG_ERR("Unsupported MAC algorithm: %s",
			current_alg.c_str());
		delete sc;
		delete options;
		return IAuthPolicySet::FAILED;
	} else {
		sc->mac_alg = current_alg;
	}

	//TODO check compression algorithm when we use them

	//Initialize Diffie-Hellman machinery and generate private/public key pairs
	if (edh_init_keys(sc) != 0) {
		delete sc;
		delete options;
		return IAuthPolicySet::FAILED;
	}

	//Add peer public key to security context
	sc->dh_peer_pub_key = BN_bin2bn(options->dh_public_key.array,
			       	        options->dh_public_key.length,
			       	        NULL);
	if (!sc->dh_peer_pub_key) {
		LOG_ERR("Error converting public key to a BIGNUM");
		delete sc;
		delete options;
		return IAuthPolicySet::FAILED;
	}

	//Options is not needed anymore
	delete options;

	//Generate the shared secret
	if (edh_generate_shared_secret(sc) != 0) {
		delete sc;
		return IAuthPolicySet::FAILED;
	}

	// Configure kernel SDU protection policy with shared secret and algorithms
	// tell it to enable decryption
	AuthStatus result = enable_encryption(sc, DECRYPTION);
	if (result == IAuthPolicySet::FAILED) {
		delete sc;
		return result;
	}

	sec_man->add_security_context(sc);
	sc->state = SSH2SecurityContext::REQUESTED_ENABLE_DECRYPTION_SERVER;
	if (result == IAuthPolicySet::SUCCESSFULL) {
		result = decryption_enabled_server(sc);
	}
	return result;
}

int AuthSSH2PolicySet::edh_generate_shared_secret(SSH2SecurityContext * sc)
{
	if((sc->shared_secret.array =
			(unsigned char*) OPENSSL_malloc(sizeof(unsigned char) * (DH_size(sc->dh_state)))) == NULL) {
		LOG_ERR("Error allocating memory for shared secret");
		return -1;
	}

	if((sc->shared_secret.length =
			DH_compute_key(sc->shared_secret.array, sc->dh_peer_pub_key, sc->dh_state)) < 0) {
		LOG_ERR("Error computing shared secret");
		return -1;
	}

	LOG_DBG("Computed shared secret of length %d: %s",
			sc->shared_secret.length,
			sc->shared_secret.toString().c_str());

	return 0;
}

IAuthPolicySet::AuthStatus AuthSSH2PolicySet::decryption_enabled_server(SSH2SecurityContext * sc)
{
	if (sc->state != SSH2SecurityContext::REQUESTED_ENABLE_DECRYPTION_SERVER) {
		LOG_ERR("Wrong state of policy");
		sec_man->destroy_security_context(sc->id);
		return IAuthPolicySet::FAILED;
	}

	LOG_DBG("Decryption enabled for port-id: %d", sc->id);

	// Prepare message for the peer, send selected algorithms and public key
	SSH2AuthOptions auth_options;
	auth_options.key_exch_algs.push_back(sc->key_exch_alg);
	auth_options.encrypt_algs.push_back(sc->encrypt_alg);
	auth_options.mac_algs.push_back(sc->mac_alg);
	auth_options.compress_algs.push_back(sc->compress_alg);
	auth_options.dh_public_key.array = BN_to_binary(sc->dh_state->pub_key,
			&auth_options.dh_public_key.length);

	if (auth_options.dh_public_key.length <= 0) {
		LOG_ERR("Error transforming big number to binary");
		sec_man->destroy_security_context(sc->id);
		return IAuthPolicySet::FAILED;
	}

	SerializedObject * sobj = encode_ssh2_auth_options(auth_options);
	if (!sobj) {
		LOG_ERR("Problems encoding SSH2AuthOptions");
		sec_man->destroy_security_context(sc->id);
		return IAuthPolicySet::FAILED;
	}

	//Send message to peer with selected algorithms and public key
	try {
		RIBObjectValue robject_value;
		robject_value.type_ = RIBObjectValue::bytestype;
		robject_value.bytes_value_ = *sobj;

		RemoteProcessId remote_id;
		remote_id.port_id_ = sc->id;

		//object class contains challenge request or reply
		//object name contains cipher name
		rib_daemon->remoteWriteObject(EDH_EXCHANGE, EDH_EXCHANGE,
				robject_value, 0, remote_id, 0);
	} catch (Exception &e) {
		LOG_ERR("Problems encoding and sending CDAP message: %s", e.what());
		sec_man->destroy_security_context(sc->id);
		delete sobj;
		return IAuthPolicySet::FAILED;
	}

	delete sobj;

	// Configure kernel SDU protection policy with shared secret and algorithms
	// tell it to enable encryption
	AuthStatus result = enable_encryption(sc, ENCRYPTION);
	if (result == IAuthPolicySet::FAILED) {
		sec_man->destroy_security_context(sc->id);
		return result;
	}

	sc->state = SSH2SecurityContext::REQUESTED_ENABLE_ENCRYPTION_SERVER;
	if (result == IAuthPolicySet::SUCCESSFULL) {
		result = encryption_enabled_server(sc);
	}
	return result;
}

IAuthPolicySet::AuthStatus AuthSSH2PolicySet::encryption_enabled_server(SSH2SecurityContext * sc)
{
	if (sc->state != SSH2SecurityContext::REQUESTED_ENABLE_ENCRYPTION_SERVER) {
		LOG_ERR("Wrong state of policy");
		sec_man->destroy_security_context(sc->id);
		return IAuthPolicySet::FAILED;
	}

	LOG_DBG("Encryption enabled for port-id: %d", sc->id);
	sc->state = SSH2SecurityContext::ENCRYPTION_SETUP_SERVER;

	return IAuthPolicySet::IN_PROGRESS;
}

int AuthSSH2PolicySet::process_incoming_message(const CDAPMessage& message, int session_id)
{
	ScopedLock sc_lock(lock);

	if (message.obj_class_ == EDH_EXCHANGE) {
		return process_edh_exchange_message(message, session_id);
	}

	return rina::IAuthPolicySet::FAILED;
}

int AuthSSH2PolicySet::process_edh_exchange_message(const CDAPMessage& message, int session_id)
{
	ByteArrayObjectValue * bytes_value;
	SSH2SecurityContext * sc;
	const SerializedObject * sobj;

	if (message.op_code_ != CDAPMessage::M_WRITE) {
		LOG_ERR("Wrong operation type");
		return IAuthPolicySet::FAILED;
	}

	if (message.obj_value_ == 0) {
		LOG_ERR("Null object value");
		return IAuthPolicySet::FAILED;
	}

	bytes_value = dynamic_cast<ByteArrayObjectValue *>(message.obj_value_);
	if (!bytes_value) {
		LOG_ERR("Object value of wrong type");
		return IAuthPolicySet::FAILED;
	}

	sobj = static_cast<const SerializedObject *>(bytes_value->get_value());
	SSH2AuthOptions * options = decode_ssh2_auth_options(*sobj);
	if (!options) {
		LOG_ERR("Could not decode SSHARSA options");
		return rina::IAuthPolicySet::FAILED;
	}

	sc = dynamic_cast<SSH2SecurityContext *>(sec_man->get_security_context(session_id));
	if (!sc) {
		LOG_ERR("Could not retrieve Security Context for session: %d", session_id);
		delete options;
		return IAuthPolicySet::FAILED;
	}

	if (sc->state != SSH2SecurityContext::WAIT_EDH_EXCHANGE) {
		LOG_ERR("Wrong session state: %d", sc->state);
		sec_man->remove_security_context(session_id);
		delete sc;
		delete options;
		return IAuthPolicySet::FAILED;
	}

	//Add peer public key to security context
	sc->dh_peer_pub_key = BN_bin2bn(options->dh_public_key.array,
			       	        options->dh_public_key.length,
			       	        NULL);
	if (!sc->dh_peer_pub_key) {
		LOG_ERR("Error converting public key to a BIGNUM");
		delete sc;
		delete options;
		return rina::IAuthPolicySet::FAILED;
	}

	//Options is not needed anymore
	delete options;

	//Generate the shared secret
	if (edh_generate_shared_secret(sc) != 0) {
		delete sc;
		return rina::IAuthPolicySet::FAILED;
	}

	// Configure kernel SDU protection policy with shared secret and algorithms
	// tell it to enable decryption and encryption
	AuthStatus result = enable_encryption(sc, ENCRYPTION_AND_DECRYPTION);
	if (result == IAuthPolicySet::FAILED) {
		sec_man->destroy_security_context(sc->id);
		return result;
	}

	sc->state = SSH2SecurityContext::REQUESTED_ENABLE_ENCRYPTION_DECRYPTION_CLIENT;
	if (result == IAuthPolicySet::SUCCESSFULL) {
		result = encryption_decryption_enabled_client(sc);
	}
	return result;
}

IAuthPolicySet::AuthStatus AuthSSH2PolicySet::encryption_decryption_enabled_client(SSH2SecurityContext * sc)
{
	if (sc->state != SSH2SecurityContext::REQUESTED_ENABLE_ENCRYPTION_DECRYPTION_CLIENT) {
		LOG_ERR("Wrong state of policy");
		sec_man->destroy_security_context(sc->id);
		return IAuthPolicySet::FAILED;
	}

	LOG_DBG("Encryption and decryption enabled for port-id: %d", sc->id);

	sc->state = SSH2SecurityContext::ENCRYPTION_SETUP_CLIENT;

	//TODO continue with authentication
	return IAuthPolicySet::IN_PROGRESS;
}

int AuthSSH2PolicySet::set_policy_set_param(const std::string& name,
                         	 	      const std::string& value)
{
        LOG_DBG("No policy-set-specific parameters to set (%s, %s)",
                        name.c_str(), value.c_str());
        return -1;
}

//Class ISecurity Manager
ISecurityManager::~ISecurityManager()
{
	std::list<IAuthPolicySet*> entries = auth_policy_sets.getEntries();
	std::list<IAuthPolicySet*>::iterator it;
	IAuthPolicySet * currentPs = 0;
	for (it = entries.begin(); it != entries.end(); ++it) {
		currentPs = (*it);
		app->psDestroy(rina::ApplicationEntity::SECURITY_MANAGER_AE_NAME,
			       currentPs->type,
			       currentPs);
	}

	std::list<ISecurityContext*> contexts = security_contexts.getEntries();
	std::list<ISecurityContext*>::iterator it2;
	ISecurityContext * context = 0;
	for (it2 = contexts.begin(); it2 != contexts.end(); ++it2) {
		context = (*it2);
		if (context) {
			delete context;
		}
	}
}

int ISecurityManager::add_auth_policy_set(const std::string& auth_type)
{
        IAuthPolicySet *candidate = NULL;

        if (!app) {
                LOG_ERR("bug: NULL app reference");
                return -1;
        }

        if (get_auth_policy_set(auth_type)) {
                LOG_INFO("authentication policy set %s already included in the security-manager",
                         auth_type.c_str());
                return 0;
        }

        candidate = (IAuthPolicySet *) app->psCreate(ApplicationEntity::SECURITY_MANAGER_AE_NAME,
        					     auth_type,
        					     this);
        if (!candidate) {
                LOG_ERR("failed to allocate instance of policy set %s", auth_type.c_str());
                return -1;
        }

        // Install the new one.
        auth_policy_sets.put(auth_type, candidate);
        LOG_INFO("Authentication policy-set %s added to the security-manager",
                        auth_type.c_str());

        return 0;
}

int ISecurityManager::set_policy_set_param(const std::string& path,
                         	 	   const std::string& name,
                         	 	   const std::string& value)
{
	IPolicySet * selected_ps;

        LOG_DBG("set_policy_set_param(%s, %s) called",
                name.c_str(), value.c_str());

        if (path == std::string()) {
        	// This request is for the component itself
        	LOG_ERR("No such parameter '%s' exists", name.c_str());
        	return -1;
        } else if (path == selected_ps_name) {
        	selected_ps = ps;
        } else {
        	selected_ps = auth_policy_sets.find(path);
        }

        if (!selected_ps) {
                LOG_ERR("Invalid component address '%s'", path.c_str());
                return -1;
        }

        return selected_ps->set_policy_set_param(name, value);
}

IAuthPolicySet * ISecurityManager::get_auth_policy_set(const std::string& auth_type)
{
	return auth_policy_sets.find(auth_type);
}

ISecurityContext * ISecurityManager::get_security_context(int context_id)
{
	return security_contexts.find(context_id);
}

ISecurityContext * ISecurityManager::remove_security_context(int context_id)
{
	return security_contexts.erase(context_id);
}

void ISecurityManager::destroy_security_context(int context_id)
{
	ISecurityContext * ctx = remove_security_context(context_id);
	if (ctx) {
		delete ctx;
	}
}

void ISecurityManager::add_security_context(ISecurityContext * context)
{
	if (!context) {
		LOG_ERR("Bogus security context passed, not storing it");
		return;
	}

	security_contexts.put(context->id, context);
}

void ISecurityManager::eventHappened(InternalEvent * event)
{
	NMinusOneFlowDeallocatedEvent * n_event = 0;
	ISecurityContext * context = 0;

	if (event->type == InternalEvent::APP_N_MINUS_1_FLOW_DEALLOCATED) {
		n_event = dynamic_cast<NMinusOneFlowDeallocatedEvent *>(event);
		context = security_contexts.erase(n_event->port_id_);
		if (context) {
			delete context;
		}
	}
}

}

