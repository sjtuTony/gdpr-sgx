#include "MessageHandler.h"
#include "sgx_tseal.h"

using namespace util;
//using namespace Messages;

MessageHandler::MessageHandler(int port) {
    //this->nm = NetworkManagerServer::getInstance(port);
    this->local_ec256_fix_data.g_key_flag = 1;
}

MessageHandler::~MessageHandler() {
    delete this->enclave;
}


/*
int MessageHandler::init() {
    this->nm->Init();
    this->nm->connectCallbackHandler([this](string v, int type) {
        return this->incomingHandler(v, type);
    });
}


void MessageHandler::start() {
    this->nm->startService();
}
*/


sgx_status_t MessageHandler::initEnclave() {
    Log("========== STATUS IS ==========");
    Log("\tmy flag is:%d",this->my_flag);
    this->enclave = Enclave::getInstance();
    sgx_status_t ret = this->enclave->createEnclave();
    if(this->my_flag == 0) {
        this->my_flag = 1;
    } 
    return ret;
}


sgx_status_t MessageHandler::getEnclaveStatus() {
    return this->enclave->getStatus();
}


uint32_t MessageHandler::getExtendedEPID_GID(uint32_t *extended_epid_group_id) {
    int ret = sgx_get_extended_epid_group_id(extended_epid_group_id);
    ret = 0;

    if (SGX_SUCCESS != ret) {
        Log("Error, call sgx_get_extended_epid_group_id fail: %lx", ret);
        print_error_message((sgx_status_t)ret);
        return ret;
    } else
        Log("Call sgx_get_extended_epid_group_id success");

    return ret;
}


string MessageHandler::generateMSG0() {
    Log("Call MSG0 generate");

    uint32_t extended_epid_group_id;
    int ret = this->getExtendedEPID_GID(&extended_epid_group_id);

    Messages::MessageMsg0 msg;
    msg.set_type(Messages::Type::RA_MSG0);

    if (ret == SGX_SUCCESS) {
        msg.set_epid(extended_epid_group_id);
    } else {
        msg.set_status(TYPE_TERMINATE);
        msg.set_epid(0);
    }
    string s;
    if(msg.SerializeToString(&s)) {
        Log("Serialization successful");
        return s;
    }
    else {
        Log("Serialization failed", log::error);
        return "";
    }
    //return "just a test";
    //return nm->serialize(msg);
}


string MessageHandler::generateMSG1() {
    int retGIDStatus = 0;
    int count = 0;
    sgx_ra_msg1_t sgxMsg1Obj;
    Log("========== SEALED ENCLAVE PUB KEY ==========");
    Log("\tgot ec256 key is:%d", local_ec256_fix_data.g_key_flag);

    while (1) {
        // read public and sealed private key from file
        ifstream pri_stream(Settings::ec_pri_key_path);
        //ifstream pri_stream_u(Settings::ec_pri_key_path_u);
        ifstream pub_stream(Settings::ec_pub_key_path);
        string pri_s_str,pub_str; 
        //string pri_s_str_u;
        getline(pri_stream,pri_s_str);
        //getline(pri_stream_u,pri_s_str_u);
        getline(pub_stream,pub_str);
        uint8_t *ppub;
        uint8_t *ppri_s;
        //uint8_t *ppri_u;
        HexStringToByteArray(pub_str,&ppub);
        HexStringToByteArray(pri_s_str,&ppri_s);
        //HexStringToByteArray(pri_s_str_u,&ppri_u);
        memcpy(&local_ec256_fix_data.ec256_public_key,ppub,sizeof(sgx_ec256_public_t));
        //memcpy(&local_ec256_fix_data.ec256_private_key,ppri_u,sizeof(sgx_ec256_private_t));
        memcpy(local_ec256_fix_data.p_sealed_data,ppri_s,592);
        local_ec256_fix_data.sealed_data_size = 592;

        Log("\tbefore public  key:%s",pub_str);
        Log("\tsealed private dat:%s",pri_s_str);


        retGIDStatus = sgx_ra_get_msg1(this->enclave->getContext(),
                                       this->enclave->getID(),
                                       sgx_ra_get_ga,
                                       &sgxMsg1Obj,
                                       &local_ec256_fix_data);
        
        unsigned char pubbuf[sizeof(sgx_ec256_public_t)];
        memcpy(pubbuf, (unsigned char*)&local_ec256_fix_data.ec256_public_key, sizeof(sgx_ec256_public_t));
        Log("\tenclave public key:%s",ByteArrayToString(pubbuf,sizeof(pubbuf)));
        unsigned char pribuf_r[sizeof(sgx_ec256_private_t)];
        memcpy(pribuf_r, (unsigned char*)&local_ec256_fix_data.ec256_private_key, sizeof(sgx_ec256_private_t));
        Log("\tenclave privat key:%s",ByteArrayToString(pribuf_r,sizeof(pribuf_r)));
        Log("\tsealed data size  :%d",local_ec256_fix_data.sealed_data_size);
        unsigned char psealedbuf[local_ec256_fix_data.sealed_data_size];
        memcpy(psealedbuf, (unsigned char*)local_ec256_fix_data.p_sealed_data, local_ec256_fix_data.sealed_data_size);
        Log("\tp sealed data is  :%s",ByteArrayToString(psealedbuf,sizeof(psealedbuf)));
        

        if (retGIDStatus == SGX_SUCCESS) {
            break;
        } else if (retGIDStatus == SGX_ERROR_BUSY) {
            if (count == 5) { //retried 5 times, so fail out
                Log("Error, sgx_ra_get_msg1 is busy - 5 retries failed", log::error);
                break;;
            } else {
                sleep(3);
                count++;
            }
        } else {    //error other than busy
            Log("Error, failed to generate MSG1,error code:%lx", retGIDStatus, log::error);
            break;
        }
    }


    if (SGX_SUCCESS == retGIDStatus) {
        Log("MSG1 generated Successfully");

        Messages::MessageMSG1 msg;
        msg.set_type(Messages::Type::RA_MSG1);

        for (auto x : sgxMsg1Obj.g_a.gx)
            msg.add_gax(x);

        for (auto x : sgxMsg1Obj.g_a.gy)
            msg.add_gay(x);

        for (auto x : sgxMsg1Obj.gid) {
            msg.add_gid(x);
        }

        string s;
        msg.SerializeToString(&s);
        return s;
        //return nm->serialize(msg);
    }

    return "";
}


void MessageHandler::assembleMSG2(Messages::MessageMSG2 msg, sgx_ra_msg2_t **pp_msg2) {
    uint32_t size = msg.size();

    sgx_ra_msg2_t *p_msg2 = NULL;
    p_msg2 = (sgx_ra_msg2_t*) malloc(size + sizeof(sgx_ra_msg2_t));

    uint8_t pub_key_gx[32];
    uint8_t pub_key_gy[32];

    sgx_ec256_signature_t sign_gb_ga;
    sgx_spid_t spid;

    for (int i; i<32; i++) {
        pub_key_gx[i] = msg.public_key_gx(i);
        pub_key_gy[i] = msg.public_key_gy(i);
    }

    for (int i=0; i<16; i++) {
        spid.id[i] = msg.spid(i);
    }

    for (int i=0; i<8; i++) {
        sign_gb_ga.x[i] = msg.signature_x(i);
        sign_gb_ga.y[i] = msg.signature_y(i);
    }

    memcpy(&p_msg2->g_b.gx, &pub_key_gx, sizeof(pub_key_gx));
    memcpy(&p_msg2->g_b.gy, &pub_key_gy, sizeof(pub_key_gy));
    memcpy(&p_msg2->sign_gb_ga, &sign_gb_ga, sizeof(sign_gb_ga));
    memcpy(&p_msg2->spid, &spid, sizeof(spid));

    p_msg2->quote_type = (uint16_t)msg.quote_type();
    p_msg2->kdf_id = msg.cmac_kdf_id();

    uint8_t smac[16];
    for (int i=0; i<16; i++)
        smac[i] = msg.smac(i);

    memcpy(&p_msg2->mac, &smac, sizeof(smac));

    p_msg2->sig_rl_size = msg.size_sigrl();
    uint8_t *sigrl = (uint8_t*) malloc(sizeof(uint8_t) * msg.size_sigrl());

    for (int i=0; i<msg.size_sigrl(); i++)
        sigrl[i] = msg.sigrl(i);

    memcpy(&p_msg2->sig_rl, &sigrl, msg.size_sigrl());

    *pp_msg2 = p_msg2;
}


//string MessageHandler::handleMSG2(string v) {
string MessageHandler::handleMSG2(Messages::MessageMSG2 msg) {
    Log("Received MSG2");
    /*
    Messages::MessageMSG2 msg;
    int retp = msg.ParseFromString(v);
    if(!retp) {
        Log("Error, Parse MSG2 failed!", log::error);
        return "";
    }
    */

    uint32_t size = msg.size();

    sgx_ra_msg2_t *p_msg2;
    this->assembleMSG2(msg, &p_msg2);

    sgx_ra_msg3_t *p_msg3 = NULL;
    uint32_t msg3_size;
    int ret = 0;

    do {
        ret = sgx_ra_proc_msg2(this->enclave->getContext(),
                               this->enclave->getID(),
                               sgx_ra_proc_msg2_trusted,
                               sgx_ra_get_msg3_trusted,
                               p_msg2,
                               size,
                               &p_msg3,
                               &msg3_size);
    } while (SGX_ERROR_BUSY == ret && busy_retry_time--);

    SafeFree(p_msg2);

    if (SGX_SUCCESS != (sgx_status_t)ret) {
        Log("Error, call sgx_ra_proc_msg2 fail, error code: %lx", ret);
    } else {
        Log("Call sgx_ra_proc_msg2 success");

        Messages::MessageMSG3 msg3;

        msg3.set_type(Messages::Type::RA_MSG3);
        msg3.set_size(msg3_size);

        for (int i=0; i<SGX_MAC_SIZE; i++)
            msg3.add_sgx_mac(p_msg3->mac[i]);

        for (int i=0; i<SGX_ECP256_KEY_SIZE; i++) {
            msg3.add_gax_msg3(p_msg3->g_a.gx[i]);
            msg3.add_gay_msg3(p_msg3->g_a.gy[i]);
        }

        for (int i=0; i<256; i++) {
            msg3.add_sec_property(p_msg3->ps_sec_prop.sgx_ps_sec_prop_desc[i]);
        }


        for (int i=0; i<1116; i++) {
            msg3.add_quote(p_msg3->quote[i]);
        }

        SafeFree(p_msg3);

        string s;
        msg3.SerializeToString(&s);
        return s;
        //return nm->serialize(msg3);
    }

    SafeFree(p_msg3);

    return "";
}


void MessageHandler::assembleAttestationMSG(Messages::AttestationMessage msg, ra_samp_response_header_t **pp_att_msg) {
    sample_ra_att_result_msg_t *p_att_result_msg = NULL;
    ra_samp_response_header_t* p_att_result_msg_full = NULL;

    int total_size = msg.size() + sizeof(ra_samp_response_header_t) + msg.result_size();
    p_att_result_msg_full = (ra_samp_response_header_t*) malloc(total_size);

    memset(p_att_result_msg_full, 0, total_size);
    p_att_result_msg_full->type = Messages::Type::RA_ATT_RESULT;
    p_att_result_msg_full->size = msg.size();

    p_att_result_msg = (sample_ra_att_result_msg_t *) p_att_result_msg_full->body;

    p_att_result_msg->platform_info_blob.sample_epid_group_status = msg.epid_group_status();
    p_att_result_msg->platform_info_blob.sample_tcb_evaluation_status = msg.tcb_evaluation_status();
    p_att_result_msg->platform_info_blob.pse_evaluation_status = msg.pse_evaluation_status();

    for (int i=0; i<PSVN_SIZE; i++)
        p_att_result_msg->platform_info_blob.latest_equivalent_tcb_psvn[i] = msg.latest_equivalent_tcb_psvn(i);

    for (int i=0; i<ISVSVN_SIZE; i++)
        p_att_result_msg->platform_info_blob.latest_pse_isvsvn[i] = msg.latest_pse_isvsvn(i);

    for (int i=0; i<PSDA_SVN_SIZE; i++)
        p_att_result_msg->platform_info_blob.latest_psda_svn[i] = msg.latest_psda_svn(i);

    for (int i=0; i<GID_SIZE; i++)
        p_att_result_msg->platform_info_blob.performance_rekey_gid[i] = msg.performance_rekey_gid(i);

    for (int i=0; i<SAMPLE_NISTP256_KEY_SIZE; i++) {
        p_att_result_msg->platform_info_blob.signature.x[i] = msg.ec_sign256_x(i);
        p_att_result_msg->platform_info_blob.signature.y[i] = msg.ec_sign256_y(i);
    }

    for (int i=0; i<SAMPLE_MAC_SIZE; i++)
        p_att_result_msg->mac[i] = msg.mac_smk(i);


    p_att_result_msg->secret.payload_size = msg.result_size();

    for (int i=0; i<12; i++)
        p_att_result_msg->secret.reserved[i] = msg.reserved(i);

    for (int i=0; i<SAMPLE_SP_TAG_SIZE; i++)
        p_att_result_msg->secret.payload_tag[i] = msg.payload_tag(i);

    for (int i=0; i<SAMPLE_SP_TAG_SIZE; i++)
        p_att_result_msg->secret.payload_tag[i] = msg.payload_tag(i);

    for (int i=0; i<msg.result_size(); i++) {
        p_att_result_msg->secret.payload[i] = (uint8_t)msg.payload(i);
    }

    *pp_att_msg = p_att_result_msg_full;
}


//string MessageHandler::handleAttestationResult(string v) {
string MessageHandler::handleAttestationResult(Messages::AttestationMessage msg) {
    Log("Received Attestation result");
    /*
    Messages::AttestationMessage msg;
    int retp = msg.ParseFromString(v);
    if(!retp) {
        Log("Error, Parse MSG2 failed!", log::error);
        return "";
    }
    */

    ra_samp_response_header_t *p_att_result_msg_full = NULL;
    this->assembleAttestationMSG(msg, &p_att_result_msg_full);
    sample_ra_att_result_msg_t *p_att_result_msg_body = (sample_ra_att_result_msg_t *) ((uint8_t*) p_att_result_msg_full + sizeof(ra_samp_response_header_t));

    sgx_status_t status;
    sgx_status_t ret;

    ret = verify_att_result_mac(this->enclave->getID(),
                                &status,
                                this->enclave->getContext(),
                                (uint8_t*)&p_att_result_msg_body->platform_info_blob,
                                sizeof(ias_platform_info_blob_t),
                                (uint8_t*)&p_att_result_msg_body->mac,
                                sizeof(sgx_mac_t));


    if ((SGX_SUCCESS != ret) || (SGX_SUCCESS != status)) {
        Log("Error: INTEGRITY FAILED - attestation result message MK based cmac failed", log::error);
        return "";
    }

    if (0 != p_att_result_msg_full->status[0] || 0 != p_att_result_msg_full->status[1]) {
        Log("Error, attestation mac result message MK based cmac failed", log::error);
    } else {
        ret = verify_secret_data(this->enclave->getID(),
                                 &status,
                                 this->enclave->getContext(),
                                 p_att_result_msg_body->secret.payload,
                                 p_att_result_msg_body->secret.payload_size,
                                 p_att_result_msg_body->secret.payload_tag,
                                 MAX_VERIFICATION_RESULT,
                                 NULL);

        SafeFree(p_att_result_msg_full);

        if (SGX_SUCCESS != ret) {
            Log("Error, attestation result message secret using SK based AESGCM failed", log::error);
            print_error_message(ret);
        } else if (SGX_SUCCESS != status) {
            Log("Error, attestation result message secret using SK based AESGCM failed", log::error);
            print_error_message(status);
        } else {
            Log("Send attestation okay");

            Messages::InitialMessage msg;
            msg.set_type(Messages::Type::RA_APP_ATT_OK);
            msg.set_size(0);

            string s;
            msg.SerializeToString(&s);
            return s;
            //return nm->serialize(msg);
        }
    }

    SafeFree(p_att_result_msg_full);

    return "";
}


//string MessageHandler::handleMSG0(string v) {
string MessageHandler::handleMSG0(Messages::MessageMsg0 msg) {
    Log("MSG0 response received");
    /*
    Messages::MessageMsg0 msg;
    int ret = msg.ParseFromString(v);
    if(!ret) {
        Log("Error, Parse MSG0 failed!", log::error);
        return "";
    }
    */

    if (msg.status() == TYPE_OK) {
        sgx_status_t ret = this->initEnclave();

        if (SGX_SUCCESS != ret || this->getEnclaveStatus()) {
            Log("Error, call enclave_init_ra fail", log::error);
        } else {
            Log("Call enclave_init_ra success");
            Log("Sending msg1 to remote attestation service provider. Expecting msg2 back");

            auto ret = this->generateMSG1();

            return ret;
        }

    } else {
        Log("MSG0 response status was not OK", log::error);
    }

    return "";
}


string MessageHandler::handleVerification() {
    Log("Verification request received");
    return this->generateMSG0();
}


string MessageHandler::createInitMsg(int type, string msg) {
    Messages::SecretMessage init_msg;
    init_msg.set_type(type);
    init_msg.set_size(msg.size());

    string s;
    init_msg.SerializeToString(&s);
    return s;
    //return nm->serialize(init_msg);
}
/*
*/


//vector<string> MessageHandler::incomingHandler(string v, int type) {
//string* MessageHandler::handleMessages(string v, int type) {
vector<string> MessageHandler::handleMessages(string v, int type) {
    vector<string> res;
    //string res[2];
    string s;
    bool ret;

    Log("========== handle messages ==========");
    switch (type) {
    case Messages::Type::RA_VERIFICATION: {	//Verification request
        Messages::InitialMessage init_msg;
        //Log("\tinit_msg:%s",v);
        ret = init_msg.ParseFromString(v);
        if (ret && init_msg.type() == Messages::Type::RA_VERIFICATION) {
            s = this->handleVerification();
            //res[0] = to_string(Messages::Type::RA_MSG0);
            res.push_back(to_string(Messages::Type::RA_MSG0));
        }
    }
    break;
    case Messages::Type::RA_MSG0: {		//Reply to MSG0
        Messages::MessageMsg0 msg0;
        ret = msg0.ParseFromString(v);
        if (ret && (msg0.type() == Messages::Type::RA_MSG0)) {
            // generate MSG1 and send to SP
            s = this->handleMSG0(msg0);
            //res[0] = to_string(Messages::Type::RA_MSG1);
            res.push_back(to_string(Messages::Type::RA_MSG1));
        }
    }
    break;
    case Messages::Type::RA_MSG2: {		//MSG2
        Messages::MessageMSG2 msg2;
        ret = msg2.ParseFromString(v);
        if (ret && (msg2.type() == Messages::Type::RA_MSG2)) {
            // generate MSG3 and send to SP
            s = this->handleMSG2(msg2);
            //res[0] = to_string(Messages::Type::RA_MSG3);
            res.push_back(to_string(Messages::Type::RA_MSG3));
        }
    }
    break;
    case Messages::Type::RA_ATT_RESULT: {	//Reply to MSG3
        Messages::AttestationMessage att_msg;
        ret = att_msg.ParseFromString(v);
        if (ret && att_msg.type() == Messages::Type::RA_ATT_RESULT) {
            // receive MSG4 and verify encrypted secret
            s = this->handleAttestationResult(att_msg);
            //res[0] = to_string(Messages::Type::RA_APP_ATT_OK);
            res.push_back(to_string(Messages::Type::RA_APP_ATT_OK));
        }
    }
    break;
    default:
        Log("Unknown type: %d", type, log::error);
        break;
    }

    //res[1] = s;
    res.push_back(s);

    return res;
}
