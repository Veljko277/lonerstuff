import hashlib, sys, re
from datetime import datetime


alphanum = "0123456789abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ_".encode()

def cstrip(lines):
	d = "".encode()
	for l in lines:
		l = re.sub("#.*".encode(), "".encode(), l)
		l = re.sub("//.*".encode(), "".encode(), l)
		d += l + " ".encode()
	d = d.replace("\t".encode(), " ".encode()) # tab to space
	d = re.sub("  *".encode(), " ".encode(), d) # remove double spaces
	d = re.sub("".encode(), "".encode(), d) # remove /* */ comments

	d = d.strip()

	# this eats up cases like 'n {'
	i = 1
	while i < len(d)-2:
		if d[i:i + 1] == " ".encode():
			if not (d[i - 1:i] in alphanum and d[i+1:i + 2] in alphanum):
				d = d[:i] + d[i + 1:]
		i += 1
	return d

f = "".encode()
for filename in sys.argv[1:]:
	f += cstrip([l.strip() for l in open(filename, "rb")])

hash = hashlib.md5(f).hexdigest().lower()[16:]
#TODO 0.7: improve nethash creation
if hash == "e42d81cd67b8c7bc":
	hash = "626fce9a778df4d4"
print('#define GAME_NETVERSION_HASH "%s"' % hash)
today = datetime.now()
date = f"{today.strftime('%B %d, %Y')}"# | Year: {today.year} | Month: {today.strftime('%B')} | Day: {today.day}"
print('#define GAME_SERVERBUILD_DATE "%s"' % date)
