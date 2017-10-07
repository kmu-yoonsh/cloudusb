#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <sys/socket.h>
#include <bluetooth/bluetooth.h>
#include <bluetooth/sdp.h>
#include <bluetooth/sdp_lib.h>
#include <bluetooth/rfcomm.h>

#define PARAM_BUF_LEN 1024

enum B_MESSAGE_ID { 
    MESG_NONE, MESG_ERROR,
    REQU_WIFI_INFO, RESP_WIFI_INFO,
    REQU_SET_WIFI, RESP_SET_WIFI,
    REQU_SET_CLIENT_SECRET, RESP_SET_CLIENT_SECRET,
    REQU_ADD_DRIVE_AUTH, RESP_ADD_DRIVE_AUTH,
    REQU_SET_DRIVE_AUTH, RESP_SET_DRIVE_AUTH
};

enum RESPONSE_STATE {
    RESULT_OK,
    RESULT_FAIL,
    RESULT_ERROR
};

typedef struct {
    int id;
    int state;
    char param1[PARAM_BUF_LEN];
    char param2[PARAM_BUF_LEN];
}BMessage;

int _str2uuid( const char *uuid_str, uuid_t *uuid ) {
    /* This is from the pybluez stack */

    uint32_t uuid_int[4];
    char *endptr;

    if( strlen( uuid_str ) == 36 ) {
        char buf[9] = { 0 };

        if( uuid_str[8] != '-' && uuid_str[13] != '-' &&
        uuid_str[18] != '-' && uuid_str[23] != '-' ) {
        return 0;
    }
    // first 8-bytes
    strncpy(buf, uuid_str, 8);
    uuid_int[0] = htonl( strtoul( buf, &endptr, 16 ) );
    if( endptr != buf + 8 ) return 0;
        // second 8-bytes
        strncpy(buf, uuid_str+9, 4);
        strncpy(buf+4, uuid_str+14, 4);
        uuid_int[1] = htonl( strtoul( buf, &endptr, 16 ) );
        if( endptr != buf + 8 ) return 0;

        // third 8-bytes
        strncpy(buf, uuid_str+19, 4);
        strncpy(buf+4, uuid_str+24, 4);
        uuid_int[2] = htonl( strtoul( buf, &endptr, 16 ) );
        if( endptr != buf + 8 ) return 0;

        // fourth 8-bytes
        strncpy(buf, uuid_str+28, 8);
        uuid_int[3] = htonl( strtoul( buf, &endptr, 16 ) );
        if( endptr != buf + 8 ) return 0;

        if( uuid != NULL ) sdp_uuid128_create( uuid, uuid_int );
    } else if ( strlen( uuid_str ) == 8 ) {
        // 32-bit reserved UUID
        uint32_t i = strtoul( uuid_str, &endptr, 16 );
        if( endptr != uuid_str + 8 ) return 0;
        if( uuid != NULL ) sdp_uuid32_create( uuid, i );
    } else if( strlen( uuid_str ) == 4 ) {
        // 16-bit reserved UUID
        int i = strtol( uuid_str, &endptr, 16 );
        if( endptr != uuid_str + 4 ) return 0;
        if( uuid != NULL ) sdp_uuid16_create( uuid, i );
    } else {
        return 0;
    }

    return 1;

}



sdp_session_t *register_service(uint8_t rfcomm_channel) {

    /* A 128-bit number used to identify this service. The words are ordered from most to least
    * significant, but within each word, the octets are ordered from least to most significant.
    * For example, the UUID represneted by this array is 00001101-0000-1000-8000-00805F9B34FB. (The
    * hyphenation is a convention specified by the Service Discovery Protocol of the Bluetooth Core
    * Specification, but is not particularly important for this program.)
    *
    * This UUID is the Bluetooth Base UUID and is commonly used for simple Bluetooth applications.
    * Regardless of the UUID used, it must match the one that the Armatus Android app is searching
    * for.
    */
    const char *service_name = "Armatus Bluetooth server";
    const char *svc_dsc = "A HERMIT server that interfaces with the Armatus Android app";
    const char *service_prov = "Armatus";

    uuid_t root_uuid, l2cap_uuid, rfcomm_uuid, svc_uuid,
           svc_class_uuid;
    sdp_list_t *l2cap_list = 0,
                *rfcomm_list = 0,
                 *root_list = 0,
                  *proto_list = 0,
                   *access_proto_list = 0,
                    *svc_class_list = 0,
                     *profile_list = 0;
    sdp_data_t *channel = 0;
    sdp_profile_desc_t profile;
    sdp_record_t record = { 0 };
    sdp_session_t *session = 0;

    // set the general service ID
    //sdp_uuid128_create(&svc_uuid, &svc_uuid_int);
    _str2uuid("00001101-0000-1000-8000-00805F9B34FB",&svc_uuid);
    sdp_set_service_id(&record, svc_uuid);

    char str[256] = "";
    sdp_uuid2strn(&svc_uuid, str, 256);
    printf("Registering UUID %s\n", str);

    // set the service class
    sdp_uuid16_create(&svc_class_uuid, SERIAL_PORT_SVCLASS_ID);
    svc_class_list = sdp_list_append(0, &svc_class_uuid);
    sdp_set_service_classes(&record, svc_class_list);

    // set the Bluetooth profile information
    sdp_uuid16_create(&profile.uuid, SERIAL_PORT_PROFILE_ID);
    profile.version = 0x0100;
    profile_list = sdp_list_append(0, &profile);
    sdp_set_profile_descs(&record, profile_list);

    // make the service record publicly browsable
    sdp_uuid16_create(&root_uuid, PUBLIC_BROWSE_GROUP);
    root_list = sdp_list_append(0, &root_uuid);
    sdp_set_browse_groups(&record, root_list);

    // set l2cap information
    sdp_uuid16_create(&l2cap_uuid, L2CAP_UUID);
    l2cap_list = sdp_list_append(0, &l2cap_uuid);
    proto_list = sdp_list_append(0, l2cap_list);

    // register the RFCOMM channel for RFCOMM sockets
    sdp_uuid16_create(&rfcomm_uuid, RFCOMM_UUID);
    channel = sdp_data_alloc(SDP_UINT8, &rfcomm_channel);
    rfcomm_list = sdp_list_append(0, &rfcomm_uuid);
    sdp_list_append(rfcomm_list, channel);
    sdp_list_append(proto_list, rfcomm_list);

    access_proto_list = sdp_list_append(0, proto_list);
    sdp_set_access_protos(&record, access_proto_list);

    // set the name, provider, and description
    sdp_set_info_attr(&record, service_name, service_prov, svc_dsc);

    // connect to the local SDP server, register the service record,
    // and disconnect
    session = sdp_connect(BDADDR_ANY, BDADDR_LOCAL, SDP_RETRY_IF_BUSY);
    sdp_record_register(session, &record, 0);

    // cleanup
    sdp_data_free(channel);
    sdp_list_free(l2cap_list, 0);
    sdp_list_free(rfcomm_list, 0);
    sdp_list_free(root_list, 0);
    sdp_list_free(access_proto_list, 0);
    sdp_list_free(svc_class_list, 0);
    sdp_list_free(profile_list, 0);

    return session;
}


int init_server() {
    int port = 3, result, sock, client, bytes_read, bytes_sent;
    struct sockaddr_rc loc_addr = { 0 }, rem_addr = { 0 };
    char buffer[1024] = { 0 };
    socklen_t opt = sizeof(rem_addr);

    // local bluetooth adapter
    loc_addr.rc_family = AF_BLUETOOTH;
    loc_addr.rc_bdaddr = *BDADDR_ANY;
    loc_addr.rc_channel = (uint8_t) port;

    // register service
    sdp_session_t *session = register_service(port);
    // allocate socket
    sock = socket(AF_BLUETOOTH, SOCK_STREAM, BTPROTO_RFCOMM);
    printf("socket() returned %d\n", sock);

    // bind socket to port 3 of the first available
    result = bind(sock, (struct sockaddr *)&loc_addr, sizeof(loc_addr));
    printf("bind() on channel %d returned %d\n", port, result);

    // put socket into listening mode
    result = listen(sock, 1);
    printf("listen() returned %d\n", result);

    //sdpRegisterL2cap(port);

    // accept one connection
    printf("calling accept()\n");
    client = accept(sock, (struct sockaddr *)&rem_addr, &opt);
    printf("accept() returned %d\n", client);

    ba2str(&rem_addr.rc_bdaddr, buffer);
    fprintf(stderr, "accepted connection from %s\n", buffer);
    memset(buffer, 0, sizeof(buffer));

    return client;
}
void parse_bmsg(char *str, BMessage *input_message){
	char *temp;
	char arr_temp[PARAM_BUF_LEN] = { 0 };

    // 1. id 
	temp = strtok(str, "$\0\n");
	strcpy(arr_temp, temp);
    input_message -> id = atoi(arr_temp);
	
    // 2. state
	temp = strtok(NULL, "$\0\n");
	strcpy(arr_temp, temp);
    	input_message -> state = atoi(arr_temp);
	

    // 3. param1
	temp = strtok(NULL, "$\0\n");
	strcpy(input_message -> param1, temp);
    // 4. param2
    temp = strtok(NULL, "$\0\n");
    strcpy(input_message -> param2, temp);
}

int set_wifi(char *ssid, char *pw){
    FILE *wpa_fp; 
    char wifi_info[PARAM_BUF_LEN] = {0};
    int ret;

    system("sudo cp /etc/wpa_supplicant/wpa_supplicant.empty /etc/wpa_supplicant/wpa_supplicant.conf");
        wpa_fp = fopen("/etc/wpa_supplicant/wpa_supplicant.conf","a");
    if(wpa_fp){
        printf("wpa file open: success\n");
    }
    else{
        printf("wpa file open: fail\n");
        return -1; 
    }

    sprintf(wifi_info, "network={\n\
    ssid=\"%s\"\n\
    psk=\"%s\"\n\
    key_mgmt=WPA-PSK\n\
}",\
            ssid, pw);
    fprintf(wpa_fp, "%s",wifi_info);
    printf("%s", wifi_info);

    fclose(wpa_fp);
    system("sudo ifdown wlan0");
    system("sudo ifup wlan0");

    return 0;
}


int get_wifi_ssid(char *ssid){
    FILE *cmd_fp;
    char cmd_return[PARAM_BUF_LEN] = {0};
    int len;

    cmd_fp = popen("iwgetid -r", "r");
    if(cmd_fp == NULL){
        perror("cmd_fp: popen() Fail");
        return -1;
    }
    fgets(cmd_return, 1024, cmd_fp);
    strcpy(ssid, cmd_return);
    len = (int)strlen(ssid);
    ssid[len-1] = '\0';

    pclose(cmd_fp);

    return 0;
}


// REQU_WIFI_INFO
int request_wifi_info(BMessage *request, BMessage *response){
    char ssid[PARAM_BUF_LEN] = { 0 };
    int ret;

    response->id = RESP_WIFI_INFO;
    ret = get_wifi_ssid(ssid);
    
    if(ret < 0){
        response->state = RESULT_ERROR;
        return -1;
    }
    else if(strlen(ssid) == 0){
        printf("\nrequest_wifi_info(): RESULT_FAIL");
        response->state = RESULT_FAIL;
        return -1;
    }
    printf("\nrequest_wifi_info(): RESULT_OK [%s]\n", ssid);
    response->state = RESULT_OK;
    strcpy(response->param1, ssid); 
    return 0;
}



// REQU_SET_WIFI
int request_set_wifi(BMessage *request, BMessage *response){
    char ssid[PARAM_BUF_LEN] = { 0 };
    char pw[PARAM_BUF_LEN] = { 0 };
    int ret;

    response->id = RESP_SET_WIFI;

    strcpy(ssid, request->param1);
    strcpy(pw, request->param2);
	ret = set_wifi(ssid, pw);
    if(ret < 0){
        response->state = RESULT_ERROR;
        return -1;
    }

	ret = get_wifi_ssid(ssid);

    response->id = RESP_SET_WIFI;
    if(ret < 0){
        response->state = RESULT_ERROR;
        return -1;
    }
	else if(strlen(ssid) == 0){
		printf("\request_set_wifi(): RESULT_FAIL");
        response->state = RESULT_FAIL;
		return -1;
	}
	printf("\request_set_wifi(): RESULT_OK [%s]\n", ssid);
    
    strcpy(response->param1, ssid);	
    response->state = RESULT_OK;
	return 0;
}

int create_client_secret_json(char *client_id, char *client_secret){
    FILE *json_fp;

    //json_fp = fopen("../../googledrive/list/client_secret.json","w");
    json_fp = fopen("client_secret.json","w");
    fprintf(json_fp,"\
{\"installed\":\n\
    {\n\
        \"client_id\":\"%s\",\n\
        \"project_id\":\"goormUSB\",\n\
        \"auth_uri\":\"https://accounts.google.com/o/oauth2/auth\",\n\
        \"token_uri\":\"https://accounts.google.com/o/oauth2/token\",\n\
        \"auth_provider_x509_cert_url\":\"https://www.googleapis.com/oauth2/v1/certs\",\n\
        \"client_secret\":\"%s\",\n\
        \"redirect_uris\":[\"urn:ietf:wg:oauth:2.0:oob\",\"http://localhost\"]\n\
    }\n\
}"\
    , client_id, client_secret);

    fclose(json_fp);
    return 0;
}

//REQU_SET_CLIENT_SECRET
int request_set_client_secret(BMessage *request, BMessage *response){
    int ret;
    char client_id[PARAM_BUF_LEN] = {0};
    char client_secret[PARAM_BUF_LEN] = {0};
    char verification_url[PARAM_BUF_LEN] = {0};

    response->id = RESP_SET_CLIENT_SECRET;

    strcpy(client_id, request->param1);
    strcpy(client_secret, request->param2);

    ret = create_client_secret_json(client_id, client_secret);
    if(ret < 0){
        response->state = RESULT_ERROR;
        return -1;
    }
    
    printf("\request_set_client_secret(): RESULT_OK");

    sprintf(verification_url, "https://accounts.google.com/o/oauth2/auth?scope=https%%3A%%2F%%2Fwww.googleapis.com%%2Fauth%%2Fdrive&redirect_uri=urn%%3Aietf%%3Awg%%3Aoauth%%3A2.0%%3Aoob&response_type=code&client_id=%s&access_type=offline", client_id);
    strcpy(response->param1, verification_url);
    response->state = RESULT_OK;
    return 0;
}

int create_drive_auth_json(char *account_nickname, char *verification_code){
    FILE *json_fp;
    char json_path[PARAM_BUF_LEN] = {0};

    // 1. 파이썬 실행
    // verification_code | python drive_auth.py --noauth_local_webserver
    // => .credentials/drive-python-quickstart.json 이 만들어짐

    // 2. 계정 추가
    // /.credentials/drive-python-quickstart.json 을
    // ./driveAuth/NICKNAME.json 으로 저장
    fclose(json_fp);
    return 0;
}

//REQU_ADD_DRIVE_AUTH
int request_add_drive_auth(BMessage *request, BMessage *response){
    int ret;
    char account_nickname[PARAM_BUF_LEN] = {0};
    char verification_code[PARAM_BUF_LEN] = {0};

    response->id = RESP_ADD_DRIVE_AUTH;

    strcpy(account_nickname, request->param1);
    strcpy(verification_code, request->param2);

    ret = create_drive_auth_json(account_nickname, verification_code);
    if(ret < 0){
        response->state = RESULT_ERROR;
        return -1;
    }
    
    printf("\request_set_client_secret(): RESULT_OK");

    sprintf(verfication_url, "https://accounts.google.com/o/oauth2/auth?scope=https%%3A%%2F%%2Fwww.googleapis.com%%2Fauth%%2Fdrive&redirect_uri=urn%%3Aietf%%3Awg%%3Aoauth%%3A2.0%%3Aoob&response_type=code&client_id=%s&access_type=offline", client_id);
    strcpy(response->param1, verfication_url);
    response->state = RESULT_OK;
    return 0;
}

int operate_message(char *command, char *response){
    int ret = 0;

    BMessage requ_message;
    BMessage resp_message = {MESG_NONE, RESULT_OK, "\0", "\0"};

    printf("received [%s]\n", command);
    parse_bmsg(command, &requ_message);

    printf("id : [%d]\n", requ_message.id);
    printf("param1 : [%s]\n", requ_message.param1);
    printf("param2 : [%s]\n", requ_message.param2);

    switch(requ_message.id){
        case REQU_WIFI_INFO:
            ret = request_wifi_info(&requ_message, &resp_message);
            break;

        case REQU_SET_WIFI:
            ret = request_set_wifi(&requ_message, &resp_message);
            break;

        case REQU_SET_CLIENT_SECRET:
            ret = request_set_client_secret(&requ_message, &resp_message);
            break;

        case REQU_ADD_DRIVE_AUTH:
            break;

        case REQU_SET_DRIVE_AUTH:
            // ./driveAuth/NICKNAME.json 을
            // /.credentials/drive-python-quickstart.json 으로 저장
            break;
    }

    sprintf(response, "%d$%d$%s$%s\n", resp_message.id, resp_message.state, resp_message.param1, resp_message.param2);
    return 0;
}

char input[PARAM_BUF_LEN] = { 0 };
char *read_server(int client) {
    // read data from the client
    int bytes_read;
    char localInput[PARAM_BUF_LEN] = { 0 };
    char response[PARAM_BUF_LEN] = { 0 };

    bytes_read = read(client, localInput, sizeof(input));
    strcpy(input, localInput);

    if (bytes_read > 0) {
    	int ret = 0;
        operate_message(input, response);
        strcpy(input, response);

        return input;
    } 
    else {
        return NULL;
    }
}




void write_server(int client, char *message) {
    // send data to the client
    char messageArr[PARAM_BUF_LEN] = { 0 };
    int bytes_sent;
    int messageLen;
    strcpy(messageArr, message);
    messageLen = (int)strlen(messageArr);

    bytes_sent = write(client, messageArr, strlen(messageArr));
    if (bytes_sent > 0) {
        printf("sent [%s] %d\n", messageArr, bytes_sent);
    }
}

int main()
{
    int client = init_server();


    while(1)
    {
        char *recv_message = read_server(client);
        if ( recv_message == NULL ){
            printf("client disconnected\n");
            break;
        }

        write_server(client, recv_message);
    }
}
