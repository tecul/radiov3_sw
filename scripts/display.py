import requests
import json
import os.path
import sys

def _url_dir(url, path):
	return url + '/api/v1/dir' + path

def dir_content(url, name, path):
	#print _url_dir(url, path)
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

def display_content(content, indent = ""):
	print indent, '+', content['name'] + ' (%s)' % content['path']
	for entry in content['content']:
		if entry['type'] == 'dir':
			display_content(entry['content'], indent + " ")
		if entry['type'] == 'file':
			print indent, ' +', entry['name'] + ' (%s)' % (content['path'] + '/' + entry['name'])

def main(url, root):
	content = dir_content_recursive(url, '', root)
	display_content(content)

if __name__ == '__main__':
	# example ./display.py http://192.168.1.10:8000 music
	main(sys.argv[1], sys.argv[2])