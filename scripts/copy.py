import requests
import json
import os.path
import sys

def _url_dir(url, path):
	return url + '/api/v1/dir' + path

def _url_upload(url, path):
	return url + '/api/v1/upload' + path

def dir_mkdir(url, path):
	resp = requests.post(_url_dir(url, path))

	return True if resp.status_code == 200 else False

def file_upload(url, remote_path, local_path):
	files = {'file': (remote_path, open(local_path, 'rb'))}
	resp = requests.post(_url_upload(url, ""), files=files)

	return True if resp.status_code == 200 else False

def remove_prefix(text, prefix):
	if text.startswith(prefix):
		return text[len(prefix):]
	return text

def dir_copy_recursive(url, remote_path, local_path):
	prefix = local_path
	dir_mkdir(url, remote_path)
	for root, dirs, files in os.walk(local_path):
		for d in dirs:
			d = remove_prefix(os.path.join(root, d), prefix)
			if d[0] == '/':
				d = d[1:]
			dir_mkdir(url, os.path.join(remote_path, d))
		for f in files:
			local_full_path = os.path.join(root, f)
			f = remove_prefix(os.path.join(root, f), prefix)
			if f[0] == '/':
				f = f[1:]
			file_upload(url, os.path.join(remote_path, f), local_full_path)

def main(url, local_dir, remote_dir):
	dir_copy_recursive(url, remote_dir, local_dir)

if __name__ == '__main__':
	# example ./copy.py http://192.168.1.10:8000 sdcard/static '/static'
	main(sys.argv[1], sys.argv[2], sys.argv[3])