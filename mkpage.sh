#!/bin/sh
# regenerate navweb_page.h from page.html (portable sed; used by "make page")
{
  echo '/* generated from page.html by mkpage.sh - do not edit */'
  echo '#ifndef NAVWEB_PAGE_H'
  echo '#define NAVWEB_PAGE_H'
  echo '#define NAVALM_WEB_VERSION "2.8g"'
  echo 'static const char navweb_page[] ='
  sed -e 's/\\/\\\\/g' -e 's/"/\\"/g' -e 's/^/"/' -e 's/$/\\n"/' page.html
  echo ';'
  echo '#endif'
} > navweb_page.h
