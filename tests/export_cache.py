import sqlite3, sys, os, math
src, out = sys.argv[1], sys.argv[2]
rows = sqlite3.connect(src).execute("SELECT z,x,y,data FROM tiles WHERE source LIKE 'https://server.arcgisonline%'").fetchall()
for f in ['t.mbtiles','t.gpkg','t.sqlite']:
    p = os.path.join(out, f)
    if os.path.exists(p): os.remove(p)
m = sqlite3.connect(os.path.join(out,'t.mbtiles'))
m.execute("CREATE TABLE metadata (name TEXT, value TEXT)")
m.execute("CREATE TABLE tiles (zoom_level INTEGER, tile_column INTEGER, tile_row INTEGER, tile_data BLOB)")
m.executemany("INSERT INTO tiles VALUES (?,?,?,?)", [(z,x,(1<<z)-1-y,d) for z,x,y,d in rows])
m.executemany("INSERT INTO metadata VALUES (?,?)", [('name','test'),('format','jpg')]); m.commit()
o = sqlite3.connect(os.path.join(out,'t.sqlite'))
o.execute("CREATE TABLE tiles (key INTEGER PRIMARY KEY, provider TEXT, tile BLOB)")
o.executemany("INSERT INTO tiles VALUES (?,?,?)", [((((z<<z)+x)<<z)+y,'test',d) for z,x,y,d in rows]); o.commit()
g = sqlite3.connect(os.path.join(out,'t.gpkg'))
H = 20037508.342789244
g.executescript("""
CREATE TABLE gpkg_spatial_ref_sys (srs_name TEXT, srs_id INTEGER PRIMARY KEY, organization TEXT, organization_coordsys_id INTEGER, definition TEXT, description TEXT);
CREATE TABLE gpkg_contents (table_name TEXT PRIMARY KEY, data_type TEXT, identifier TEXT, description TEXT, last_change TEXT, min_x REAL, min_y REAL, max_x REAL, max_y REAL, srs_id INTEGER);
CREATE TABLE gpkg_tile_matrix_set (table_name TEXT PRIMARY KEY, srs_id INTEGER, min_x REAL, min_y REAL, max_x REAL, max_y REAL);
CREATE TABLE gpkg_tile_matrix (table_name TEXT, zoom_level INTEGER, matrix_width INTEGER, matrix_height INTEGER, tile_width INTEGER, tile_height INTEGER, pixel_x_size REAL, pixel_y_size REAL);
CREATE TABLE imagery (id INTEGER PRIMARY KEY, zoom_level INTEGER, tile_column INTEGER, tile_row INTEGER, tile_data BLOB);
""")
g.execute("INSERT INTO gpkg_spatial_ref_sys VALUES ('WGS 84 / Pseudo-Mercator',3857,'EPSG',3857,'','')")
g.execute("INSERT INTO gpkg_contents VALUES ('imagery','tiles','imagery','','',?,?,?,?,3857)", (-H,-H,H,H))
g.execute("INSERT INTO gpkg_tile_matrix_set VALUES ('imagery',3857,?,?,?,?)", (-H,-H,H,H))
zs = sorted({r[0] for r in rows})
g.executemany("INSERT INTO gpkg_tile_matrix VALUES ('imagery',?,?,?,256,256,?,?)", [(z,1<<z,1<<z,2*H/(256*(1<<z)),2*H/(256*(1<<z))) for z in zs])
g.executemany("INSERT INTO imagery (zoom_level,tile_column,tile_row,tile_data) VALUES (?,?,?,?)", rows); g.commit()
print(len(rows), 'tiles, zooms', zs[0], '-', zs[-1])
