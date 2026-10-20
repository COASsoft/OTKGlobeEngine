import html
import json
import sys

releases_json, page = sys.argv[1], sys.argv[2]
releases = [r for r in json.load(open(releases_json, encoding='utf-8')) if not r.get('draft')]
releases.sort(key=lambda r: r.get('published_at') or '', reverse=True)

parts = []
for i, r in enumerate(releases[:5]):
    name = html.escape(r.get('name') or r['tag_name'])
    date = (r.get('published_at') or '')[:10]
    parts.append(f"<h3>{name} ({date}){' - latest' if i == 0 else ''}</h3>")
    parts.append(f"<p><a href={r['html_url']}>Release notes</a></p>")
    parts.append('<ul>')
    for a in sorted(r.get('assets', []), key=lambda a: a['name'].lower()):
        size = a['size'] / 1048576.0
        parts.append(f"<li><a href={a['browser_download_url']}>{html.escape(a['name'])}</a> ({size:.1f} MB)</li>")
    parts.append('</ul>')
if not parts:
    parts.append('<p>No releases yet.</p>')

text = open(page, encoding='utf-8').read().replace('@RELEASES@', '\n'.join(parts))
open(page, 'w', encoding='utf-8', newline='\n').write(text)
