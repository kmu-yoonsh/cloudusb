# -*- coding: utf-8 -*-
from __future__ import print_function
import httplib2
import os
import sys
import errno
import io

from apiclient import discovery
from oauth2client import client
from oauth2client import tools
from oauth2client.file import Storage
from apiclient.http import MediaIoBaseDownload


PIPE_PATH = ''

try:
    import argparse

    tools.argparser.add_argument('--path', default='./../myfifo', help='pipe path')

    flags = argparse.ArgumentParser(parents=[tools.argparser]).parse_args()

    if flags.path:
        PIPE_PATH = flags.path

except ImportError:
    flags = None

reload(sys)
sys.setdefaultencoding('utf-8')

# 접근 권한: https://developers.google.com/drive/v2/web/about-auth
SCOPES = 'https://www.googleapis.com/auth/drive.readonly'

CLIENT_SECRET_FILE = 'client_secret.json'
APPLICATION_NAME = 'Drive API Python Quickstart'
FOLDER = "application/vnd.google-apps.folder"  # 구글 드라이브 API에선 타입이 이 스트링인 파일을 폴더로 인식함
ROOT_FOLDER = "cloud_usb_test"  # 테스트를 위한 최상위 폴더

#구글 계정 권한에 대한 API
# quickstart.json -> .credentials 
def get_credentials():
    home_dir = os.path.expanduser('~')
    credential_dir = os.path.join(home_dir, '.credentials')
    if not os.path.exists(credential_dir):
        os.makedirs(credential_dir)
    credential_path = os.path.join(credential_dir,
                                   'drive-python-quickstart.json')

    store = Storage(credential_path)
    credentials = store.get()
    if not credentials or credentials.invalid:
        flow = client.flow_from_clientsecrets(CLIENT_SECRET_FILE, SCOPES)
        flow.user_agent = APPLICATION_NAME
        if flags:
            credentials = tools.run_flow(flow, store, flags)
        else:  # Needed only for compatibility with Python 2.6
            credentials = tools.run(flow, store)
        print('Storing credentials to ' + credential_path)
    return credentials

# def metaDataListsortKey(keyValue):
#    return (keyValue.split())[1]

def main():
    credentials = get_credentials()
    http = credentials.authorize(httplib2.Http())
    service = discovery.build('drive', 'v3', http=http)

    # === 17.01.31 ===#
    # API DOC: list() 에 들어가는 파라미터들(orderBy, q, fields 등)에 대한 문서 
    #   https://developers.google.com/resources/api-libraries/documentation/drive/v3/python/latest/index.html
    #
    # QUERY: list() 안에서 q="" 에 들어가는 쿼리문에 대한 문서
    #   https://developers.google.com/drive/v3/web/search-parameters
    #


    # 1. ROOT_DIRECTORY 이름을 가진 최상위 폴더를 찾음
    first_folder = service.files().list(
        q=("mimeType = 'application/vnd.google-apps.folder' and name = '%s'" % ROOT_FOLDER)).execute()
    first_folder_item = first_folder.get('files', [])
    root_dir_id = 0
    if not first_folder_item:
        print('No %s found.' % ROOT_FOLDER)
    else:
        for item in first_folder_item:
            root_dir_id = item['id']

    # 2. 최상위 폴더부터 시작해서 모든 파일, 디렉토리 정보를 탐색
    result_files = []
    listing_files_with_download(service, root_dir_id, "", result_files)

    # sorted_result_directories = result_files
    # sorted_result_files= sorted(result_files, reverse=False, key=metaDataListsortKey)


    # 3. 탐색한 파일, 디렉토리 정보를 보여줌
    for file in result_files:
        print(file)

    # 4. 파일, 디렉토리 정보를 파이프에 저장
    try:
        os.mkfifo(PIPE_PATH)
    except OSError as exc:
        if exc.errno != errno.EEXIST:
            raise exc
        pass

    fifo = open(PIPE_PATH, "w")    
    try:
        for file in result_files:
            fifo.write(file + "\n")
    finally:
        fifo.close()



def listing_files_with_download(service, folderID, directory, result_files):
    results = service.files().list(
        orderBy="folder desc, createdTime",
        q=("'%s' in parents and trashed = false " % folderID),
        fields="files(id, name, mimeType, size)").execute()
    items = results.get('files', [])
    if not items:
        # result_files.append('%s : No files found.'%directory)
        pass
    else:
        for item in items:
            # 이름에 공백 있으면 _ 으로 치환
            item['name'] = item['name'].replace(" ", "_")

            if item['mimeType'] == FOLDER:
                result_files.append('%s %s %s %s' % (directory + '/' + item['name'], "1", "0", "1"))
                listing_files_with_download(service, item['id'], directory + "/%s" % item['name'], result_files)
            else:
                result_files.append('%s %s %s %s' % (directory + '/' + item['name'], item['size'] ,item['id'], '0'))
                if( not (os.path.isfile('./'+item['id'])) ):
                    partial_download(service, item['id'], 0, int(item['size']), './')
                elif( os.path.getsize('./'+item['id']) != int(item['size']) ):
                    print(item['name'] + ": exist, but loss size!")
                    partial_download(service, item['id'], 0, int(item['size']), './')
                else:
                    print(item['name'] + ": exist, Pass!")


def partial_download(service, file_id, byte_begin, byte_end, file_path):
    drive_file = service.files().get(fileId=file_id, fields='size, id, name').execute()

    download_url = service.files().get_media(fileId=file_id).uri
    # print("download_url:"+ download_url)
    total_size = int(drive_file.get('size'))
    uploadedFileName = drive_file.get('name')
    filename = file_path + file_id

    # print("file_path:"+ file_path)
    if download_url:
        with open(filename, 'wb') as file:
            print("downloaded: ")
            headers = {"Range": 'bytes=%s-%s' % (byte_begin, byte_end)}
            resp, content = service._http.request(download_url, headers=headers)
            if resp.status == 206:
                file.write(content)
                file.flush()
            else:
                print('An error occurred: %s' % resp)
                return None
            print(uploadedFileName + " - offset: (" + str(byte_begin) + ", " + str(byte_end) + "), size: " +str(byte_end - byte_begin) + " [Success!]")
        return filename
    else:
        return None

if __name__ == '__main__':
    main()