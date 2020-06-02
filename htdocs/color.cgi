#!/bin/bash

COLOR="blue"
PRELEN=6

echo "Content-type: text/html"
echo ""
echo '<html><head>'
echo '<meta http-equiv="Content-Type" content="text/html; charset=UTF-8">'
echo '<title>CGI Demo: COLOR</title></head>'

if [ "$CONTENT_LENGTH" -gt 6 ]
 then
 read -n $PRELEN TMP <&0
 read -n $((CONTENT_LENGTH - PRELEN)) COLOR <&0
fi
echo "<body style="background-color:$COLOR">"
echo "This is $COLOR"
echo '</body></html>'
exit 0
