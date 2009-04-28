-- Community tags

CREATE TABLE IF NOT EXISTS tag (
	name TEXT NOT NULL
);

CREATE UNIQUE INDEX tag_name_idx ON tag(name);


CREATE TABLE IF NOT EXISTS track_tag (
	track INTEGER NOT NULL,
	tag INTEGER NOT NULL,
	weight FLOAT NOT NULL
);

CREATE INDEX track_tag_track_idx ON track_tag(track);
CREATE INDEX track_tag_tag_idx ON track_tag(tag);
