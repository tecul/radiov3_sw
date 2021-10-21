import sys
import os
import subprocess

def build_manifest_files(root_dir, version):
	res = []
	for root, dirs, files in os.walk(os.path.join(root_dir, "sdcard/static")):
		for file in files:
			filename = os.path.relpath(os.path.join(root, file), root_dir)
			urlname = "https://raw.githubusercontent.com/tecul/radiov3_sw/%s/%s" % (version, filename)
			filename = "/sdcard/update/" + filename
			res.append((urlname, filename))

	return res

def main(root_dir, version, filename):
	files = build_manifest_files(root_dir, version)
	f = open(filename, 'w')
	for (url, file) in files:
		f.write("%s %s\n" % (url, file))
	f.close()

if __name__ == '__main__':
	version = subprocess.check_output(['git', 'describe', '--abbrev=8', "--long", "--always"])
	main(os.getcwd(), version.strip().decode("utf-8")[-8:], "build/ota_matifest.txt")