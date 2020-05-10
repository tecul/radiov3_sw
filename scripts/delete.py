import requests
import json
import os.path
import sys

def _url_dir(url, path):
	return url + '/api/v1/dir' + path

def _url_file(url, path):
	return url + '/api/v1/file' + path

def file_unlink(url, path):
	resp = requests.delete(_url_file(url, path))

	return True if resp.status_code == 200 else False

def dir_rmdir(url, path):
	resp = requests.delete(_url_dir(url, path))

	return True if resp.status_code == 200 else False

def dir_content(url, name, path):
	resp = requests.get(_url_dir(url, path))
	if resp.status_code != 200:
		return None

	return {'type': 'dir', 'name': name, 'path': path, 'content': resp.json()}

def dir_content_recursive(url, name, path):
	content = dir_content(url, name, path)

	for entry in content['content']:
		if entry['type'] == 'dir':
			entry['content'] = dir_content_recursive(url, entry['name'], path + '/' + entry['name'])

	return content

def dir_rmdir_recursive(url, path):
	content = dir_content_recursive(url, '', path)
	for entry in content['content']:
		if entry['type'] == 'dir':
			dir_rmdir_recursive(url, content['path'] + '/' + entry['name'])
		if entry['type'] == 'file':
			file_unlink(url, content['path'] + '/' + entry['name'])
	dir_rmdir(url, path)

def main(url, remote_path):
	dir_rmdir_recursive(url, remote_path)

if __name__ == '__main__':
	# example ./delete.py http://192.168.1.10:8000 /static
	main(sys.argv[1], sys.argv[2])