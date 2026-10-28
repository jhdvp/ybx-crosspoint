// Feed fixtures shaped like the YBX server's aggregated OPDS library
// (opds-library-all.md in the YBX server repository): root navigation ->
// «Все»/sources -> shelf or series list -> signed book cards. The firmware
// browses that tree with the SDK's Atom parser, so these tests pin what the
// device sees per level.
#include <gtest/gtest.h>

#include <cstddef>
#include <string>

#include "OpdsEntry.h"
#include "OpdsParser.h"

namespace {

constexpr const char* BASE = "https://my.ybx.app/opds/mcz2f88vx9vxjqkphtua3nnv";

bool contains(const std::string& haystack, const char* needle) { return haystack.find(needle) != std::string::npos; }

void parse(OpdsParser& parser, const char* xml) {
  const std::string feed(xml);
  parser.write(reinterpret_cast<const uint8_t*>(feed.data()), feed.size());
  parser.flush();
  ASSERT_FALSE(parser.error());
}

// Root catalog: the merged library first, then each connected source folder.
constexpr const char* ROOT_FEED = R"XML(<?xml version='1.0' encoding='utf-8'?>
<feed xmlns="http://www.w3.org/2005/Atom" xmlns:opensearch="http://a9.com/-/spec/opensearch/1.1/" xmlns:dcterms="http://purl.org/dc/terms/" xmlns:calibre="http://calibre.kovidgoyal.net/2009/metadata">
  <id>urn:xteink-yandex:nav:mcz2f88vx9vxjqkphtua3nnv</id>
  <title>YBX</title>
  <updated>2026-10-20T10:00:00Z</updated>
  <author><name>YBX</name></author>
  <link href="https://my.ybx.app/opds/mcz2f88vx9vxjqkphtua3nnv/search/description.xml" rel="search" title="Поиск" type="application/opensearchdescription+xml"/>
  <link href="https://my.ybx.app/opds/mcz2f88vx9vxjqkphtua3nnv/search?q={searchTerms}" rel="search" title="Поиск" type="application/atom+xml"/>
  <link href="https://my.ybx.app/opds/mcz2f88vx9vxjqkphtua3nnv" rel="start" type="application/atom+xml;profile=opds-catalog;kind=navigation"/>
  <link href="https://my.ybx.app/opds/mcz2f88vx9vxjqkphtua3nnv" rel="self" type="application/atom+xml;profile=opds-catalog;kind=navigation"/>
  <entry>
    <id>urn:xteink-yandex:nav:library</id>
    <title>Все</title>
    <updated>2026-10-20T10:00:00Z</updated>
    <author><name>YBX</name></author>
    <content type="text">Одна библиотека: книги всех подключённых источников</content>
    <link href="https://my.ybx.app/opds/mcz2f88vx9vxjqkphtua3nnv/library" rel="subsection" type="application/atom+xml;profile=opds-catalog;kind=navigation"/>
    <link href="https://my.ybx.app/opds/mcz2f88vx9vxjqkphtua3nnv/library" rel="alternate" type="application/atom+xml;profile=opds-catalog;kind=navigation"/>
  </entry>
  <entry>
    <id>urn:xteink-yandex:nav:source-yandex_books</id>
    <title>Яндекс Книги</title>
    <updated>2026-10-20T10:00:00Z</updated>
    <author><name>YBX</name></author>
    <content type="text">Библиотека источника «Яндекс Книги»</content>
    <link href="https://my.ybx.app/opds/mcz2f88vx9vxjqkphtua3nnv/sources/yandex_books" rel="subsection" type="application/atom+xml;profile=opds-catalog;kind=navigation"/>
    <link href="https://my.ybx.app/opds/mcz2f88vx9vxjqkphtua3nnv/sources/yandex_books" rel="alternate" type="application/atom+xml;profile=opds-catalog;kind=navigation"/>
  </entry>
  <entry>
    <id>urn:xteink-yandex:nav:source-litres</id>
    <title>ЛитРес</title>
    <updated>2026-10-20T10:00:00Z</updated>
    <author><name>YBX</name></author>
    <content type="text">Библиотека источника «ЛитРес»</content>
    <link href="https://my.ybx.app/opds/mcz2f88vx9vxjqkphtua3nnv/sources/litres" rel="subsection" type="application/atom+xml;profile=opds-catalog;kind=navigation"/>
    <link href="https://my.ybx.app/opds/mcz2f88vx9vxjqkphtua3nnv/sources/litres" rel="alternate" type="application/atom+xml;profile=opds-catalog;kind=navigation"/>
  </entry>
  <entry>
    <id>urn:xteink-yandex:nav:source-wb</id>
    <title>WB Книги</title>
    <updated>2026-10-20T10:00:00Z</updated>
    <author><name>YBX</name></author>
    <content type="text">Библиотека источника «WB Книги»</content>
    <link href="https://my.ybx.app/opds/mcz2f88vx9vxjqkphtua3nnv/sources/wb" rel="subsection" type="application/atom+xml;profile=opds-catalog;kind=navigation"/>
    <link href="https://my.ybx.app/opds/mcz2f88vx9vxjqkphtua3nnv/sources/wb" rel="alternate" type="application/atom+xml;profile=opds-catalog;kind=navigation"/>
  </entry>
</feed>)XML";

// The «Все» folder: one row per merged shelf, with no counts on purpose.
constexpr const char* LIBRARY_FOLDERS_FEED = R"XML(<?xml version='1.0' encoding='utf-8'?>
<feed xmlns="http://www.w3.org/2005/Atom" xmlns:opensearch="http://a9.com/-/spec/opensearch/1.1/" xmlns:dcterms="http://purl.org/dc/terms/" xmlns:calibre="http://calibre.kovidgoyal.net/2009/metadata">
  <id>urn:ybx:all:nav:mcz2f88vx9vxjqkphtua3nnv</id>
  <title>Все</title>
  <updated>2026-10-20T10:00:00Z</updated>
  <author><name>YBX</name></author>
  <link href="https://my.ybx.app/opds/mcz2f88vx9vxjqkphtua3nnv/search/description.xml" rel="search" title="Поиск" type="application/opensearchdescription+xml"/>
  <link href="https://my.ybx.app/opds/mcz2f88vx9vxjqkphtua3nnv/search?q={searchTerms}" rel="search" title="Поиск" type="application/atom+xml"/>
  <link href="https://my.ybx.app/opds/mcz2f88vx9vxjqkphtua3nnv" rel="start" type="application/atom+xml;profile=opds-catalog;kind=navigation"/>
  <link href="https://my.ybx.app/opds/mcz2f88vx9vxjqkphtua3nnv" rel="up" type="application/atom+xml;profile=opds-catalog;kind=navigation"/>
  <link href="https://my.ybx.app/opds/mcz2f88vx9vxjqkphtua3nnv/library" rel="self" type="application/atom+xml;profile=opds-catalog;kind=navigation"/>
  <entry>
    <id>urn:xteink-yandex:nav:library-reading</id>
    <title>Читаю</title>
    <updated>2026-10-20T10:00:00Z</updated>
    <author><name>YBX</name></author>
    <content type="text">Книги, которые вы читаете сейчас</content>
    <link href="https://my.ybx.app/opds/mcz2f88vx9vxjqkphtua3nnv/library/reading" rel="subsection" type="application/atom+xml;profile=opds-catalog;kind=acquisition"/>
    <link href="https://my.ybx.app/opds/mcz2f88vx9vxjqkphtua3nnv/library/reading" rel="alternate" type="application/atom+xml;profile=opds-catalog;kind=acquisition"/>
  </entry>
  <entry>
    <id>urn:xteink-yandex:nav:library-want-to-read</id>
    <title>Хочу прочитать</title>
    <updated>2026-10-20T10:00:00Z</updated>
    <author><name>YBX</name></author>
    <content type="text">Книги, отложенные на потом</content>
    <link href="https://my.ybx.app/opds/mcz2f88vx9vxjqkphtua3nnv/library/want-to-read" rel="subsection" type="application/atom+xml;profile=opds-catalog;kind=acquisition"/>
    <link href="https://my.ybx.app/opds/mcz2f88vx9vxjqkphtua3nnv/library/want-to-read" rel="alternate" type="application/atom+xml;profile=opds-catalog;kind=acquisition"/>
  </entry>
  <entry>
    <id>urn:xteink-yandex:nav:library-finished</id>
    <title>Прочитано</title>
    <updated>2026-10-20T10:00:00Z</updated>
    <author><name>YBX</name></author>
    <content type="text">Книги, которые вы прочитали</content>
    <link href="https://my.ybx.app/opds/mcz2f88vx9vxjqkphtua3nnv/library/finished" rel="subsection" type="application/atom+xml;profile=opds-catalog;kind=acquisition"/>
    <link href="https://my.ybx.app/opds/mcz2f88vx9vxjqkphtua3nnv/library/finished" rel="alternate" type="application/atom+xml;profile=opds-catalog;kind=acquisition"/>
  </entry>
  <entry>
    <id>urn:xteink-yandex:nav:library-series</id>
    <title>Серии</title>
    <updated>2026-10-20T10:00:00Z</updated>
    <author><name>YBX</name></author>
    <content type="text">Серии книг всех источников</content>
    <link href="https://my.ybx.app/opds/mcz2f88vx9vxjqkphtua3nnv/library/series" rel="subsection" type="application/atom+xml;profile=opds-catalog;kind=navigation"/>
    <link href="https://my.ybx.app/opds/mcz2f88vx9vxjqkphtua3nnv/library/series" rel="alternate" type="application/atom+xml;profile=opds-catalog;kind=navigation"/>
  </entry>
  <entry>
    <id>urn:xteink-yandex:nav:library-shelves</id>
    <title>Полки</title>
    <updated>2026-10-20T10:00:00Z</updated>
    <author><name>YBX</name></author>
    <content type="text">Книжные полки Яндекс Книг</content>
    <link href="https://my.ybx.app/opds/mcz2f88vx9vxjqkphtua3nnv/shelves" rel="subsection" type="application/atom+xml;profile=opds-catalog;kind=navigation"/>
    <link href="https://my.ybx.app/opds/mcz2f88vx9vxjqkphtua3nnv/shelves" rel="alternate" type="application/atom+xml;profile=opds-catalog;kind=navigation"/>
  </entry>
  <entry>
    <id>urn:xteink-yandex:nav:library-uploaded</id>
    <title>Загруженные</title>
    <updated>2026-10-20T10:00:00Z</updated>
    <author><name>YBX</name></author>
    <content type="text">Книги, загруженные вами на сервер YBX или в Яндекс Книги</content>
    <link href="https://my.ybx.app/opds/mcz2f88vx9vxjqkphtua3nnv/library/uploaded" rel="subsection" type="application/atom+xml;profile=opds-catalog;kind=acquisition"/>
    <link href="https://my.ybx.app/opds/mcz2f88vx9vxjqkphtua3nnv/library/uploaded" rel="alternate" type="application/atom+xml;profile=opds-catalog;kind=acquisition"/>
  </entry>
</feed>)XML";

// One merged shelf: an outage notice first, then signed book cards.
constexpr const char* SHELF_FEED = R"XML(<?xml version='1.0' encoding='utf-8'?>
<feed xmlns="http://www.w3.org/2005/Atom" xmlns:opensearch="http://a9.com/-/spec/opensearch/1.1/" xmlns:dcterms="http://purl.org/dc/terms/" xmlns:calibre="http://calibre.kovidgoyal.net/2009/metadata">
  <id>urn:ybx:all:reading:mcz2f88vx9vxjqkphtua3nnv</id>
  <title>Читаю</title>
  <subtitle>Книги, которые вы читаете сейчас</subtitle>
  <updated>2026-10-20T10:00:00Z</updated>
  <author><name>YBX</name></author>
  <link href="https://my.ybx.app/opds/mcz2f88vx9vxjqkphtua3nnv/search/description.xml" rel="search" title="Поиск по всем источникам" type="application/opensearchdescription+xml"/>
  <link href="https://my.ybx.app/opds/mcz2f88vx9vxjqkphtua3nnv/search?q={searchTerms}" rel="search" title="Поиск по всем источникам" type="application/atom+xml"/>
  <link href="https://my.ybx.app/opds/mcz2f88vx9vxjqkphtua3nnv/library" rel="start" type="application/atom+xml;profile=opds-catalog;kind=navigation"/>
  <link href="https://my.ybx.app/opds/mcz2f88vx9vxjqkphtua3nnv/library" rel="up" type="application/atom+xml;profile=opds-catalog;kind=navigation"/>
  <link href="https://my.ybx.app/opds/mcz2f88vx9vxjqkphtua3nnv/library/reading" rel="self" type="application/atom+xml;profile=opds-catalog;kind=acquisition"/>
  <link href="https://my.ybx.app/opds/mcz2f88vx9vxjqkphtua3nnv/library/reading" rel="first" type="application/atom+xml;profile=opds-catalog;kind=acquisition"/>
  <link href="https://my.ybx.app/opds/mcz2f88vx9vxjqkphtua3nnv/library/reading?page=3" rel="last" type="application/atom+xml;profile=opds-catalog;kind=acquisition"/>
  <link href="https://my.ybx.app/opds/mcz2f88vx9vxjqkphtua3nnv/library/reading?page=2" rel="next" type="application/atom+xml;profile=opds-catalog;kind=acquisition"/>
  <opensearch:totalResults>45</opensearch:totalResults>
  <entry>
    <id>urn:ybx:notice:yandex-unavailable</id>
    <title>Яндекс Книги недоступны</title>
    <updated>2026-10-20T10:00:00Z</updated>
    <author><name>YBX</name></author>
    <content type="text">Яндекс Книги временно не отвечают. Обновите подключение в кабинете YBX.</content>
    <link href="https://my.ybx.app/opds/mcz2f88vx9vxjqkphtua3nnv/login" rel="subsection" type="application/atom+xml;profile=opds-catalog;kind=navigation"/>
    <link href="https://my.ybx.app/opds/mcz2f88vx9vxjqkphtua3nnv/login" rel="alternate" type="application/atom+xml;profile=opds-catalog;kind=navigation"/>
  </entry>
  <entry>
    <id>urn:ybx:book:yandex:1e9a</id>
    <title>[30%] Сага. Книга вторая</title>
    <updated>2026-10-20T10:00:00Z</updated>
    <author><name>Иван Петров</name></author>
    <content type="text">Серия: Сага (книга 2)
Прогресс в YBX: 30%
Вторая часть саги о северных землях.</content>
    <calibre:series>Сага</calibre:series>
    <calibre:series_index>2</calibre:series_index>
    <category term="Сага" label="Сага" scheme="http://www.calibre-ebook.com/labels"/>
    <dcterms:extent>30%</dcterms:extent>
    <link href="https://my.ybx.app/opds/mcz2f88vx9vxjqkphtua3nnv/books/yandex/1e9a.epub?e=1760000000&amp;s=0123456789abcdef" rel="http://opds-spec.org/acquisition" type="application/epub+zip"/>
    <link href="https://my.ybx.app/opds/mcz2f88vx9vxjqkphtua3nnv/books/yandex/1e9a.epub.meta.json?e=1760000000&amp;s=0123456789abcdef" rel="related" type="application/json"/>
    <link href="https://my.ybx.app/opds/mcz2f88vx9vxjqkphtua3nnv/covers/yandex/1e9a.jpg" rel="http://opds-spec.org/image" type="image/jpeg"/>
    <link href="https://my.ybx.app/opds/mcz2f88vx9vxjqkphtua3nnv/covers/yandex/1e9a-thumb.jpg" rel="http://opds-spec.org/image/thumbnail" type="image/jpeg"/>
    <link href="https://my.ybx.app/opds/mcz2f88vx9vxjqkphtua3nnv/books/litres/77aa.epub?e=1760000000&amp;s=fedcba9876543210" rel="alternate" type="application/epub+zip" title="ЛитРес"/>
  </entry>
  <entry>
    <id>urn:ybx:book:litres:77aa</id>
    <title>Отдельная книга</title>
    <updated>2026-10-20T10:00:00Z</updated>
    <author><name>Мария Соколова</name></author>
    <dcterms:extent>0%</dcterms:extent>
    <link href="https://my.ybx.app/opds/mcz2f88vx9vxjqkphtua3nnv/books/litres/77aa.epub?e=1760000000&amp;s=fedcba9876543210" rel="http://opds-spec.org/acquisition" type="application/epub+zip"/>
    <link href="https://my.ybx.app/opds/mcz2f88vx9vxjqkphtua3nnv/covers/litres/77aa-thumb.jpg" rel="http://opds-spec.org/image/thumbnail" type="image/jpeg"/>
    <link href="https://my.ybx.app/opds/mcz2f88vx9vxjqkphtua3nnv/covers/litres/77aa.jpg" rel="http://opds-spec.org/image" type="image/jpeg"/>
  </entry>
</feed>)XML";

// Series across sources: one row per series, counts in the title, paged.
constexpr const char* SERIES_FEED = R"XML(<?xml version='1.0' encoding='utf-8'?>
<feed xmlns="http://www.w3.org/2005/Atom" xmlns:opensearch="http://a9.com/-/spec/opensearch/1.1/" xmlns:dcterms="http://purl.org/dc/terms/" xmlns:calibre="http://calibre.kovidgoyal.net/2009/metadata">
  <id>urn:ybx:all:series:mcz2f88vx9vxjqkphtua3nnv</id>
  <title>Серии</title>
  <updated>2026-10-20T10:00:00Z</updated>
  <author><name>YBX</name></author>
  <link href="https://my.ybx.app/opds/mcz2f88vx9vxjqkphtua3nnv/search/description.xml" rel="search" title="Поиск по всем источникам" type="application/opensearchdescription+xml"/>
  <link href="https://my.ybx.app/opds/mcz2f88vx9vxjqkphtua3nnv/search?q={searchTerms}" rel="search" title="Поиск по всем источникам" type="application/atom+xml"/>
  <link href="https://my.ybx.app/opds/mcz2f88vx9vxjqkphtua3nnv/library" rel="start" type="application/atom+xml;profile=opds-catalog;kind=navigation"/>
  <link href="https://my.ybx.app/opds/mcz2f88vx9vxjqkphtua3nnv/library" rel="up" type="application/atom+xml;profile=opds-catalog;kind=navigation"/>
  <link href="https://my.ybx.app/opds/mcz2f88vx9vxjqkphtua3nnv/library/series" rel="self" type="application/atom+xml;profile=opds-catalog;kind=navigation"/>
  <link href="https://my.ybx.app/opds/mcz2f88vx9vxjqkphtua3nnv/library/series" rel="first" type="application/atom+xml;profile=opds-catalog;kind=navigation"/>
  <link href="https://my.ybx.app/opds/mcz2f88vx9vxjqkphtua3nnv/library/series?page=2" rel="last" type="application/atom+xml;profile=opds-catalog;kind=navigation"/>
  <link href="https://my.ybx.app/opds/mcz2f88vx9vxjqkphtua3nnv/library/series?page=2" rel="next" type="application/atom+xml;profile=opds-catalog;kind=navigation"/>
  <opensearch:totalResults>3</opensearch:totalResults>
  <entry>
    <id>urn:xteink-yandex:nav:series-saga</id>
    <title>Сага (1)</title>
    <updated>2026-10-20T10:00:00Z</updated>
    <author><name>YBX</name></author>
    <content type="text">Автор: Иван Петров</content>
    <link href="https://my.ybx.app/opds/mcz2f88vx9vxjqkphtua3nnv/library/series/series-saga" rel="subsection" type="application/atom+xml;profile=opds-catalog;kind=acquisition"/>
    <link href="https://my.ybx.app/opds/mcz2f88vx9vxjqkphtua3nnv/library/series/series-saga" rel="alternate" type="application/atom+xml;profile=opds-catalog;kind=acquisition"/>
  </entry>
  <entry>
    <id>urn:xteink-yandex:nav:series-chronicles</id>
    <title>Хроники (3)</title>
    <updated>2026-10-20T10:00:00Z</updated>
    <author><name>YBX</name></author>
    <link href="https://my.ybx.app/opds/mcz2f88vx9vxjqkphtua3nnv/library/series/series-chronicles" rel="subsection" type="application/atom+xml;profile=opds-catalog;kind=acquisition"/>
    <link href="https://my.ybx.app/opds/mcz2f88vx9vxjqkphtua3nnv/library/series/series-chronicles" rel="alternate" type="application/atom+xml;profile=opds-catalog;kind=acquisition"/>
  </entry>
</feed>)XML";

// One series opened: books in series order, EPUB first, FB3 optional.
constexpr const char* SERIES_BOOKS_FEED = R"XML(<?xml version='1.0' encoding='utf-8'?>
<feed xmlns="http://www.w3.org/2005/Atom" xmlns:opensearch="http://a9.com/-/spec/opensearch/1.1/" xmlns:dcterms="http://purl.org/dc/terms/" xmlns:calibre="http://calibre.kovidgoyal.net/2009/metadata">
  <id>urn:ybx:all:series:series-chronicles</id>
  <title>Хроники</title>
  <subtitle>Серия · Иван Петров</subtitle>
  <updated>2026-10-20T10:00:00Z</updated>
  <author><name>YBX</name></author>
  <link href="https://my.ybx.app/opds/mcz2f88vx9vxjqkphtua3nnv/search/description.xml" rel="search" title="Поиск по всем источникам" type="application/opensearchdescription+xml"/>
  <link href="https://my.ybx.app/opds/mcz2f88vx9vxjqkphtua3nnv/search?q={searchTerms}" rel="search" title="Поиск по всем источникам" type="application/atom+xml"/>
  <link href="https://my.ybx.app/opds/mcz2f88vx9vxjqkphtua3nnv/library" rel="start" type="application/atom+xml;profile=opds-catalog;kind=navigation"/>
  <link href="https://my.ybx.app/opds/mcz2f88vx9vxjqkphtua3nnv/library/series" rel="up" type="application/atom+xml;profile=opds-catalog;kind=navigation"/>
  <link href="https://my.ybx.app/opds/mcz2f88vx9vxjqkphtua3nnv/library/series/series-chronicles" rel="self" type="application/atom+xml;profile=opds-catalog;kind=acquisition"/>
  <link href="https://my.ybx.app/opds/mcz2f88vx9vxjqkphtua3nnv/library/series/series-chronicles" rel="first" type="application/atom+xml;profile=opds-catalog;kind=acquisition"/>
  <link href="https://my.ybx.app/opds/mcz2f88vx9vxjqkphtua3nnv/library/series/series-chronicles" rel="last" type="application/atom+xml;profile=opds-catalog;kind=acquisition"/>
  <opensearch:totalResults>3</opensearch:totalResults>
  <entry>
    <id>urn:ybx:book:yandex:h1</id>
    <title>Хроники. Начало</title>
    <updated>2026-10-20T10:00:00Z</updated>
    <author><name>Иван Петров</name></author>
    <content type="text">Серия: Хроники (книга 1)
Прогресс в YBX: 100%</content>
    <calibre:series>Хроники</calibre:series>
    <calibre:series_index>1</calibre:series_index>
    <dcterms:extent>100%</dcterms:extent>
    <link href="https://my.ybx.app/opds/mcz2f88vx9vxjqkphtua3nnv/books/yandex/h1.epub?e=1760000000&amp;s=aaaa" rel="http://opds-spec.org/acquisition" type="application/epub+zip"/>
    <link href="https://my.ybx.app/opds/mcz2f88vx9vxjqkphtua3nnv/books/yandex/h1.fb3?e=1760000000&amp;s=aaaa" rel="http://opds-spec.org/acquisition" type="application/fb3"/>
    <link href="https://my.ybx.app/opds/mcz2f88vx9vxjqkphtua3nnv/covers/yandex/h1.jpg" rel="http://opds-spec.org/image" type="image/jpeg"/>
  </entry>
  <entry>
    <id>urn:ybx:book:litres:h2</id>
    <title>Хроники. Разлом</title>
    <updated>2026-10-20T10:00:00Z</updated>
    <author><name>Иван Петров</name></author>
    <content type="text">Серия: Хроники (книга 2)</content>
    <calibre:series>Хроники</calibre:series>
    <calibre:series_index>2</calibre:series_index>
    <dcterms:extent>0%</dcterms:extent>
    <link href="https://my.ybx.app/opds/mcz2f88vx9vxjqkphtua3nnv/books/litres/h2.epub?e=1760000000&amp;s=bbbb" rel="http://opds-spec.org/acquisition" type="application/epub+zip"/>
    <link href="https://my.ybx.app/opds/mcz2f88vx9vxjqkphtua3nnv/books/yandex/h2.epub?e=1760000000&amp;s=cccc" rel="alternate" type="application/epub+zip" title="Яндекс Книги"/>
    <link href="https://my.ybx.app/opds/mcz2f88vx9vxjqkphtua3nnv/covers/litres/h2.jpg" rel="http://opds-spec.org/image" type="image/jpeg"/>
  </entry>
  <entry>
    <id>urn:ybx:book:yandex:h10</id>
    <title>Хроники. Финал</title>
    <updated>2026-10-20T10:00:00Z</updated>
    <author><name>Иван Петров</name></author>
    <content type="text">Серия: Хроники (книга 10)</content>
    <calibre:series>Хроники</calibre:series>
    <calibre:series_index>10</calibre:series_index>
    <dcterms:extent>0%</dcterms:extent>
    <link href="https://my.ybx.app/opds/mcz2f88vx9vxjqkphtua3nnv/books/yandex/h10.epub?e=1760000000&amp;s=dddd" rel="http://opds-spec.org/acquisition" type="application/epub+zip"/>
    <link href="https://my.ybx.app/opds/mcz2f88vx9vxjqkphtua3nnv/covers/yandex/h10.jpg" rel="http://opds-spec.org/image" type="image/jpeg"/>
  </entry>
</feed>)XML";

// Search: one feed, each source's hits under its own section header.
constexpr const char* SEARCH_FEED = R"XML(<?xml version='1.0' encoding='utf-8'?>
<feed xmlns="http://www.w3.org/2005/Atom" xmlns:opensearch="http://a9.com/-/spec/opensearch/1.1/" xmlns:dcterms="http://purl.org/dc/terms/" xmlns:calibre="http://calibre.kovidgoyal.net/2009/metadata">
  <id>urn:ybx:search:mcz2f88vx9vxjqkphtua3nnv</id>
  <title>Поиск: хроники</title>
  <subtitle>Поиск по всем источникам</subtitle>
  <updated>2026-10-20T10:00:00Z</updated>
  <author><name>YBX</name></author>
  <link href="https://my.ybx.app/opds/mcz2f88vx9vxjqkphtua3nnv/search/description.xml" rel="search" title="Поиск по всем источникам" type="application/opensearchdescription+xml"/>
  <link href="https://my.ybx.app/opds/mcz2f88vx9vxjqkphtua3nnv/search?q={searchTerms}" rel="search" title="Поиск по всем источникам" type="application/atom+xml"/>
  <link href="https://my.ybx.app/opds/mcz2f88vx9vxjqkphtua3nnv/library" rel="start" type="application/atom+xml;profile=opds-catalog;kind=navigation"/>
  <link href="https://my.ybx.app/opds/mcz2f88vx9vxjqkphtua3nnv/search?q=%D1%85%D1%80%D0%BE%D0%BD%D0%B8%D0%BA%D0%B8" rel="http://opds-spec.org/facet" title="Сначала новые" type="application/atom+xml;profile=opds-catalog;kind=acquisition" opensearch:facetGroup="Сортировка" opensearch:activeFacet="true"/>
  <link href="https://my.ybx.app/opds/mcz2f88vx9vxjqkphtua3nnv/search?q=%D1%85%D1%80%D0%BE%D0%BD%D0%B8%D0%BA%D0%B8&amp;sort=popular" rel="http://opds-spec.org/facet" title="По популярности" type="application/atom+xml;profile=opds-catalog;kind=acquisition" opensearch:facetGroup="Сортировка"/>
  <link href="https://my.ybx.app/opds/mcz2f88vx9vxjqkphtua3nnv/search?q=%D1%85%D1%80%D0%BE%D0%BD%D0%B8%D0%BA%D0%B8" rel="first" type="application/atom+xml;profile=opds-catalog;kind=acquisition"/>
  <link href="https://my.ybx.app/opds/mcz2f88vx9vxjqkphtua3nnv/search?q=%D1%85%D1%80%D0%BE%D0%BD%D0%B8%D0%BA%D0%B8&amp;page=2" rel="last" type="application/atom+xml;profile=opds-catalog;kind=acquisition"/>
  <link href="https://my.ybx.app/opds/mcz2f88vx9vxjqkphtua3nnv/search?q=%D1%85%D1%80%D0%BE%D0%BD%D0%B8%D0%BA%D0%B8&amp;page=2" rel="next" type="application/atom+xml;profile=opds-catalog;kind=acquisition"/>
  <opensearch:totalResults>13</opensearch:totalResults>
  <entry>
    <id>urn:ybx:search:yandex</id>
    <title>Яндекс Книги (2)</title>
    <updated>2026-10-20T10:00:00Z</updated>
    <author><name>Яндекс Книги</name></author>
    <content type="text">Найдено в Яндекс Книгах</content>
    <link href="https://my.ybx.app/opds/mcz2f88vx9vxjqkphtua3nnv/sources/yandex_books" rel="subsection" type="application/atom+xml;profile=opds-catalog;kind=acquisition"/>
    <link href="https://my.ybx.app/opds/mcz2f88vx9vxjqkphtua3nnv/sources/yandex_books" rel="alternate" type="application/atom+xml;profile=opds-catalog;kind=acquisition"/>
  </entry>
  <entry>
    <id>urn:ybx:book:yandex:h1</id>
    <title>Хроники. Начало</title>
    <updated>2026-10-20T10:00:00Z</updated>
    <author><name>Иван Петров</name></author>
    <content type="text">Серия: Хроники (книга 1)</content>
    <link href="https://my.ybx.app/opds/mcz2f88vx9vxjqkphtua3nnv/books/yandex/h1.epub?e=1760000000&amp;s=aaaa" rel="http://opds-spec.org/acquisition" type="application/epub+zip"/>
  </entry>
  <entry>
    <id>urn:ybx:book:yandex:h2</id>
    <title>Хроники. Разлом</title>
    <updated>2026-10-20T10:00:00Z</updated>
    <author><name>Иван Петров</name></author>
    <link href="https://my.ybx.app/opds/mcz2f88vx9vxjqkphtua3nnv/books/yandex/h2.epub?e=1760000000&amp;s=bbbb" rel="http://opds-spec.org/acquisition" type="application/epub+zip"/>
  </entry>
  <entry>
    <id>urn:ybx:search:litres</id>
    <title>ЛитРес (1)</title>
    <updated>2026-10-20T10:00:00Z</updated>
    <author><name>ЛитРес</name></author>
    <link href="https://my.ybx.app/opds/mcz2f88vx9vxjqkphtua3nnv/sources/litres" rel="subsection" type="application/atom+xml;profile=opds-catalog;kind=acquisition"/>
    <link href="https://my.ybx.app/opds/mcz2f88vx9vxjqkphtua3nnv/sources/litres" rel="alternate" type="application/atom+xml;profile=opds-catalog;kind=acquisition"/>
  </entry>
  <entry>
    <id>urn:ybx:book:litres:h2</id>
    <title>Хроники. Разлом</title>
    <updated>2026-10-20T10:00:00Z</updated>
    <author><name>Иван Петров</name></author>
    <link href="https://my.ybx.app/opds/mcz2f88vx9vxjqkphtua3nnv/books/litres/h2.epub?e=1760000000&amp;s=cccc" rel="http://opds-spec.org/acquisition" type="application/epub+zip"/>
  </entry>
</feed>)XML";

// A source that does not hand out files: the server gives the card a
// text/html alternate instead of an acquisition.
constexpr const char* NO_ACQUISITION_FEED = R"XML(<?xml version='1.0' encoding='utf-8'?>
<feed xmlns="http://www.w3.org/2005/Atom" xmlns:opensearch="http://a9.com/-/spec/opensearch/1.1/" xmlns:dcterms="http://purl.org/dc/terms/" xmlns:calibre="http://calibre.kovidgoyal.net/2009/metadata">
  <id>urn:ybx:source:wb</id>
  <title>WB Книги</title>
  <updated>2026-10-20T10:00:00Z</updated>
  <author><name>YBX</name></author>
  <link href="https://my.ybx.app/opds/mcz2f88vx9vxjqkphtua3nnv/sources/wb" rel="self" type="application/atom+xml;profile=opds-catalog;kind=acquisition"/>
  <opensearch:totalResults>1</opensearch:totalResults>
  <entry>
    <id>urn:ybx:book:wb:zz</id>
    <title>Книга без выдачи файла</title>
    <updated>2026-10-20T10:00:00Z</updated>
    <author><name>Пётр Орлов</name></author>
    <dcterms:extent>0%</dcterms:extent>
    <link href="https://www.wildberries.ru/catalog/zz/detail.aspx" rel="alternate" type="text/html" hreflang="ru"/>
  </entry>
</feed>)XML";

TEST(OpdsLibraryTree, RootListsMergedLibraryBeforeSourceFolders) {
  OpdsParser parser;
  parse(parser, ROOT_FEED);

  EXPECT_EQ(parser.getFeedTitle(), "YBX");
  // Every level keeps search: the inline template drives the keyboard, the
  // description document is the fallback other servers provide instead.
  EXPECT_EQ(parser.getSearchTemplate(), std::string(BASE) + "/search?q={searchTerms}");
  EXPECT_EQ(parser.getSearchDescriptionUrl(), std::string(BASE) + "/search/description.xml");
  EXPECT_TRUE(parser.getNextPageUrl().empty());
  EXPECT_TRUE(parser.getPrevPageUrl().empty());

  const auto& entries = parser.getEntries();
  ASSERT_EQ(entries.size(), 4u);
  EXPECT_EQ(entries[0].title, "Все");
  EXPECT_EQ(entries[0].type, OpdsEntryType::NAVIGATION);
  EXPECT_EQ(entries[0].href, std::string(BASE) + "/library");
  // The merged library is the first row; each source folder follows.
  EXPECT_EQ(entries[1].title, "Яндекс Книги");
  EXPECT_EQ(entries[1].href, std::string(BASE) + "/sources/yandex_books");
  EXPECT_EQ(entries[2].title, "ЛитРес");
  EXPECT_EQ(entries[2].href, std::string(BASE) + "/sources/litres");
  EXPECT_EQ(entries[3].title, "WB Книги");
  EXPECT_EQ(entries[3].href, std::string(BASE) + "/sources/wb");
  EXPECT_EQ(entries[3].type, OpdsEntryType::NAVIGATION);
}

TEST(OpdsLibraryTree, AllFolderIsNavigationUntilAShelfIsOpened) {
  OpdsParser parser;
  parse(parser, LIBRARY_FOLDERS_FEED);

  EXPECT_EQ(parser.getFeedTitle(), "Все");
  EXPECT_EQ(parser.getSearchTemplate(), std::string(BASE) + "/search?q={searchTerms}");

  const auto& entries = parser.getEntries();
  ASSERT_EQ(entries.size(), 6u);
  const char* const titles[] = {"Читаю", "Хочу прочитать", "Прочитано", "Серии", "Полки", "Загруженные"};
  const char* const hrefs[] = {"/library/reading", "/library/want-to-read", "/library/finished", "/library/series",
                               "/shelves",         "/library/uploaded"};
  for (size_t i = 0; i < entries.size(); i++) {
    EXPECT_EQ(entries[i].title, titles[i]) << i;
    EXPECT_EQ(entries[i].type, OpdsEntryType::NAVIGATION) << i;
    EXPECT_EQ(entries[i].href, std::string(BASE) + hrefs[i]) << i;
  }
}

TEST(OpdsLibraryTree, ShelfFeedShowsOutageNoticeThenSignedBookCards) {
  OpdsParser parser;
  parse(parser, SHELF_FEED);

  EXPECT_EQ(parser.getFeedTitle(), "Читаю");
  const auto& entries = parser.getEntries();
  ASSERT_EQ(entries.size(), 3u);

  // A dead source contributes a tappable message row, not a hole in the list.
  EXPECT_EQ(entries[0].type, OpdsEntryType::NAVIGATION);
  EXPECT_EQ(entries[0].title, "Яндекс Книги недоступны");
  EXPECT_EQ(entries[0].href, std::string(BASE) + "/login");

  // Download links are signed and relative to the feed; no credentials in them.
  const std::string signedHref = std::string(BASE) + "/books/yandex/1e9a.epub?e=1760000000&s=0123456789abcdef";
  EXPECT_EQ(entries[1].type, OpdsEntryType::BOOK);
  EXPECT_EQ(entries[1].title, "[30%] Сага. Книга вторая");
  EXPECT_EQ(entries[1].author, "Иван Петров");
  EXPECT_EQ(entries[1].href, signedHref);
  // The progress-annotated annotation reaches the details page as plain text.
  EXPECT_TRUE(contains(entries[1].description, "Серия: Сага (книга 2)"));
  EXPECT_TRUE(contains(entries[1].description, "Прогресс в YBX: 30%"));
  EXPECT_TRUE(contains(entries[1].description, "Вторая часть саги"));
  // Full cover wins over the thumbnail regardless of link order.
  EXPECT_EQ(entries[1].coverHref, std::string(BASE) + "/covers/yandex/1e9a.jpg");

  EXPECT_EQ(entries[2].type, OpdsEntryType::BOOK);
  EXPECT_EQ(entries[2].href, std::string(BASE) + "/books/litres/77aa.epub?e=1760000000&s=fedcba9876543210");
  EXPECT_TRUE(entries[2].description.empty());
  EXPECT_EQ(entries[2].coverHref, std::string(BASE) + "/covers/litres/77aa.jpg");

  // Pagination follows the feed's own links. YBX reports totalResults but not
  // itemsPerPage/startIndex, so the "N/M" indicator stays unknown while the
  // arrow bar still works.
  EXPECT_EQ(parser.getFirstPageUrl(), std::string(BASE) + "/library/reading");
  EXPECT_EQ(parser.getLastPageUrl(), std::string(BASE) + "/library/reading?page=3");
  EXPECT_EQ(parser.getNextPageUrl(), std::string(BASE) + "/library/reading?page=2");
  EXPECT_TRUE(parser.getPrevPageUrl().empty());
  EXPECT_EQ(parser.currentPage(), 0);
  EXPECT_EQ(parser.pageCount(), 0);
}

TEST(OpdsLibraryTree, SeriesFeedListsSeriesWithCounts) {
  OpdsParser parser;
  parse(parser, SERIES_FEED);

  EXPECT_EQ(parser.getFeedTitle(), "Серии");
  const auto& entries = parser.getEntries();
  ASSERT_EQ(entries.size(), 2u);
  EXPECT_EQ(entries[0].title, "Сага (1)");
  EXPECT_EQ(entries[0].type, OpdsEntryType::NAVIGATION);
  EXPECT_EQ(entries[0].href, std::string(BASE) + "/library/series/series-saga");
  EXPECT_EQ(entries[1].title, "Хроники (3)");
  EXPECT_EQ(entries[1].href, std::string(BASE) + "/library/series/series-chronicles");

  EXPECT_EQ(parser.getNextPageUrl(), std::string(BASE) + "/library/series?page=2");
  EXPECT_EQ(parser.getLastPageUrl(), std::string(BASE) + "/library/series?page=2");
  EXPECT_TRUE(parser.getPrevPageUrl().empty());
}

TEST(OpdsLibraryTree, SeriesBooksStayInSeriesOrderWithEpubFirst) {
  OpdsParser parser;
  parse(parser, SERIES_BOOKS_FEED);

  EXPECT_EQ(parser.getFeedTitle(), "Хроники");
  const auto& entries = parser.getEntries();
  ASSERT_EQ(entries.size(), 3u);
  const char* const titles[] = {"Хроники. Начало", "Хроники. Разлом", "Хроники. Финал"};
  for (size_t i = 0; i < entries.size(); i++) {
    EXPECT_EQ(entries[i].type, OpdsEntryType::BOOK) << i;
    EXPECT_EQ(entries[i].title, titles[i]) << i;
  }
  // The FB3 acquisition after the EPUB must not replace it.
  EXPECT_EQ(entries[0].href, std::string(BASE) + "/books/yandex/h1.epub?e=1760000000&s=aaaa");
  // An alternate copy stays an alternate; the preferred source's link wins.
  EXPECT_EQ(entries[1].href, std::string(BASE) + "/books/litres/h2.epub?e=1760000000&s=bbbb");
  EXPECT_EQ(entries[2].href, std::string(BASE) + "/books/yandex/h10.epub?e=1760000000&s=dddd");
  // The series number stays visible in the card's description.
  EXPECT_TRUE(contains(entries[2].description, "(книга 10)"));
}

TEST(OpdsLibraryTree, SearchFeedKeepsSourceHeadersAndSortFacets) {
  OpdsParser parser;
  parse(parser, SEARCH_FEED);

  EXPECT_EQ(parser.getFeedTitle(), "Поиск: хроники");
  const auto& entries = parser.getEntries();
  ASSERT_EQ(entries.size(), 5u);
  EXPECT_EQ(entries[0].type, OpdsEntryType::NAVIGATION);
  EXPECT_EQ(entries[0].title, "Яндекс Книги (2)");
  EXPECT_EQ(entries[0].href, std::string(BASE) + "/sources/yandex_books");
  EXPECT_EQ(entries[1].type, OpdsEntryType::BOOK);
  EXPECT_EQ(entries[2].type, OpdsEntryType::BOOK);
  EXPECT_EQ(entries[3].type, OpdsEntryType::NAVIGATION);
  EXPECT_EQ(entries[3].title, "ЛитРес (1)");
  EXPECT_EQ(entries[4].type, OpdsEntryType::BOOK);

  // Sort options ride as facets, grouped under one heading.
  const auto facets = parser.takeFacetEntries();
  ASSERT_EQ(facets.size(), 2u);
  EXPECT_EQ(facets[0].title, "Сначала новые");
  EXPECT_EQ(facets[0].heading, "Сортировка");
  EXPECT_EQ(facets[1].title, "По популярности");
  EXPECT_TRUE(facets[1].heading.empty());
  EXPECT_EQ(facets[1].href, std::string(BASE) + "/search?q=%D1%85%D1%80%D0%BE%D0%BD%D0%B8%D0%BA%D0%B8&sort=popular");
  EXPECT_EQ(parser.getNextPageUrl(), std::string(BASE) + "/search?q=%D1%85%D1%80%D0%BE%D0%BD%D0%B8%D0%BA%D0%B8&page=2");
}

// The server only uses this shape when a source does not hand out its files
// (build_library_feed(acquisition=False)). The SDK parser keeps an entry only
// when an acquisition or navigation link chose a href, so the row is hidden
// rather than shown as a card that cannot be downloaded.
TEST(OpdsLibraryTree, EntriesWithoutAnAcquisitionAreDropped) {
  OpdsParser parser;
  parse(parser, NO_ACQUISITION_FEED);

  EXPECT_TRUE(parser.getEntries().empty());
  EXPECT_FALSE(parser.error());
}

TEST(OpdsLibraryTree, ATreeLevelStopsAtTheEntryBudget) {
  std::string feed =
      "<?xml version='1.0' encoding='utf-8'?><feed xmlns=\"http://www.w3.org/2005/Atom\"><id>urn:ybx:big</id>"
      "<title>YBX</title><updated>2026-10-20T10:00:00Z</updated>";
  for (int i = 0; i < 100; i++) {
    feed += "<entry><id>urn:ybx:book:" + std::to_string(i) + "</id><title>Книга " + std::to_string(i) +
            "</title><link href=\"https://my.ybx.app/opds/dev/books/" + std::to_string(i) +
            ".epub\" rel=\"http://opds-spec.org/acquisition\" type=\"application/epub+zip\"/></entry>";
  }
  feed += "</feed>";

  OpdsParser parser;
  parse(parser, feed.c_str());
  EXPECT_EQ(parser.getEntries().size(), OpdsLimits::MAX_ENTRIES);
  EXPECT_TRUE(parser.truncated());
  EXPECT_FALSE(parser.error());
  EXPECT_EQ(parser.getFeedTitle(), "YBX");
}

}  // namespace
