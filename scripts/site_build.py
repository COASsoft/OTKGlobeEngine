import argparse
import html
import json
import os
import posixpath
import re
import shutil

import markdown

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))

DOCS = {
    'README.md': ('overview.html', 'Overview'),
    'docs/EMBEDDING.md': ('embedding.html', 'Embedding'),
    'docs/FEATURES.md': ('features.html', 'Features'),
    'docs/VIEWER.md': ('viewer.html', 'Viewer and building'),
    'docs/FORMATS.md': ('formats.html', 'Formats'),
    'docs/OFFLINE.md': ('offline.html', 'Offline'),
    'docs/PHOTOREALISM.md': ('photorealism.html', '3D Tiles'),
    'docs/XR.md': ('xr.html', 'VR and AR'),
    'docs/MILITARY.md': ('military.html', 'Military'),
    'docs/THIRD_PARTY.md': ('third-party.html', 'Third-party licenses'),
    'CHANGELOG.md': ('changelog.html', 'Changelog'),
    'SECURITY.md': ('security.html', 'Security'),
}

PAGES = {
    'index.html': 'Home',
    'screenshots.html': 'Screenshots',
    'download.html': 'Download',
}


def nav(current, repo, gitlab):
    groups = [
        ('About', [('index.html', 'Home'), ('screenshots.html', 'Screenshots'), ('overview.html', 'Overview')]),
        ('Get it', [('download.html', 'Download'), ('changelog.html', 'Changelog')]),
        ('Docs', [('embedding.html', 'Embedding'), ('features.html', 'Features'), ('viewer.html', 'Viewer and building'), ('formats.html', 'Formats'), ('offline.html', 'Offline'),
                  ('photorealism.html', '3D Tiles'), ('xr.html', 'VR and AR'), ('military.html', 'Military')]),
        ('Project', [('security.html', 'Security'), ('license.html', 'License'), ('third-party.html', 'Third-party licenses'),
                     (f'https://github.com/{repo}/issues', 'Issues'), (f'https://github.com/{repo}', 'GitHub'),
                     (f'https://gitlab.com/{gitlab}', 'GitLab')]),
    ]
    out = []
    for title, links in groups:
        out.append(f'<b>{title}</b>\n<ul>')
        for href, label in links:
            cls = ' class=here' if href == current else (' class=out' if href.startswith('https://') else '')
            out.append(f'  <li><a{cls} href="{href}">{label}</a></li>')
        out.append('</ul>')
    return '\n'.join(out)


def page(name, title, content, current, repo, gitlab):
    return f'''<!doctype html>
<html lang=en>
<head>
<meta charset=utf-8>
<meta name=viewport content="width=device-width, initial-scale=1">
<title>{html.escape(title)}</title>
<link rel=stylesheet href=style.css>
</head>
<body>
<header><a class=brand href=index.html>{html.escape(name)}</a><span class=tag>A 3D map engine for TAK-style applications</span></header>
<div class=page>
<nav>
{nav(current, repo, gitlab)}
</nav>
<main>
{content}
</main>
</div>
<footer>{html.escape(name)} is licensed under the Apache License 2.0. It is an independent project and is not affiliated with tak.gov or the TAK Product Center.</footer>
</body>
</html>
'''


def rewrite_links(text, source, repo):
    base = posixpath.dirname(source)

    def fix(m):
        attr, url = m.group(1), m.group(2)
        if re.match(r'^[a-zA-Z][a-zA-Z0-9+.-]*:', url) or url.startswith('#'):
            return m.group(0)
        path, _, frag = url.partition('#')
        target = posixpath.normpath(posixpath.join(base, path))
        if target in DOCS:
            new = DOCS[target][0]
        elif target == 'LICENSE':
            new = 'license.html'
        elif target.startswith('screenshots/'):
            new = 'shots/' + posixpath.basename(target)
        elif target.startswith('docs/images/'):
            new = 'images/' + posixpath.basename(target)
        else:
            new = f'https://github.com/{repo}/blob/main/{target}'
        if frag:
            new += '#' + frag
        return f'{attr}="{new}"'

    return re.sub(r'(href|src)="([^"]*)"', fix, text)


def releases_html(path):
    if not path or not os.path.exists(path):
        return '<p>No releases yet.</p>'
    releases = [r for r in json.load(open(path, encoding='utf-8')) if not r.get('draft')]
    releases.sort(key=lambda r: r.get('published_at') or '', reverse=True)
    parts = []
    for i, r in enumerate(releases[:5]):
        name = html.escape(r.get('name') or r['tag_name'])
        date = (r.get('published_at') or '')[:10]
        parts.append(f"<h3>{name} ({date}){' - latest' if i == 0 else ''}</h3>")
        parts.append(f"<p><a href=\"{r['html_url']}\">Release notes</a></p>")
        parts.append('<ul>')
        for a in sorted(r.get('assets', []), key=lambda a: a['name'].lower()):
            size = f"{a['size'] / 1048576.0:.1f} MB" if a['size'] >= 1048576 else f"{max(1, round(a['size'] / 1024.0))} KB"
            parts.append(f"<li><a href=\"{a['browser_download_url']}\">{html.escape(a['name'])}</a> ({size})</li>")
        parts.append('</ul>')
    return '\n'.join(parts) if parts else '<p>No releases yet.</p>'


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('out')
    ap.add_argument('--releases', default='')
    ap.add_argument('--repo', default=os.environ.get('GITHUB_REPOSITORY', 'grangedevgroup-code/OTKGlobeEngine'))
    ap.add_argument('--gitlab', default='')
    args = ap.parse_args()
    repo = args.repo
    name = repo.split('/')[-1]
    gitlab = args.gitlab or f'grangedevgroup/{name}'
    out = args.out
    os.makedirs(os.path.join(out, 'shots'), exist_ok=True)
    os.makedirs(os.path.join(out, 'images'), exist_ok=True)
    shutil.copy(os.path.join(ROOT, 'site', 'style.css'), out)
    shots = os.path.join(ROOT, 'screenshots', 'otkglobeengine')
    for f in os.listdir(shots):
        if f.lower().endswith('.png'):
            shutil.copy(os.path.join(shots, f), os.path.join(out, 'shots', f))
    images = os.path.join(ROOT, 'docs', 'images')
    if os.path.isdir(images):
        for f in os.listdir(images):
            shutil.copy(os.path.join(images, f), os.path.join(out, 'images', f))

    def substitute(text):
        return text.replace('@NAME@', name).replace('@REPO@', repo).replace('@GITLAB@', gitlab)

    for file, title in PAGES.items():
        content = substitute(open(os.path.join(ROOT, 'site', file), encoding='utf-8').read())
        if file == 'download.html':
            content = content.replace('@RELEASES@', releases_html(args.releases))
        full = page(name, f'{name} - {title}' if file != 'index.html' else name, content, file, repo, gitlab)
        open(os.path.join(out, file), 'w', encoding='utf-8', newline='\n').write(full)

    for source, (target, title) in DOCS.items():
        path = os.path.join(ROOT, *source.split('/'))
        if not os.path.exists(path):
            continue
        body = markdown.markdown(open(path, encoding='utf-8').read(), extensions=['tables', 'fenced_code', 'toc', 'sane_lists'])
        body = rewrite_links(body, source, repo)
        full = page(name, f'{name} - {title}', body, target, repo, gitlab)
        open(os.path.join(out, target), 'w', encoding='utf-8', newline='\n').write(full)

    license_text = html.escape(open(os.path.join(ROOT, 'LICENSE'), encoding='utf-8').read())
    content = f'<h1>License</h1>\n<pre>{license_text}</pre>'
    open(os.path.join(out, 'license.html'), 'w', encoding='utf-8', newline='\n').write(
        page(name, f'{name} - License', content, 'license.html', repo, gitlab))


if __name__ == '__main__':
    main()
