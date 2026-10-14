"""Translate controller Browse into bounded, read-only MA commands and compact pages."""
import time
from hashlib import sha256

KINDS={"albums", "artists", "playlists", "artist_albums", "album_tracks", "playlist_tracks", "folders"}

def validate_browse(args):
    if not isinstance(args,dict) or set(args)-{"kind","provider","parent_id","source","path","offset","limit"}:
        raise ValueError("Invalid browse arguments")
    if args.get("kind") not in KINDS:
        raise ValueError("Unknown browse kind")
    for key in ("provider","parent_id","source","path"):
        if key in args and (not isinstance(args[key],str) or not args[key] or len(args[key])>512):
            raise ValueError("Invalid browse identity")
    for key, default, maximum in (("offset",0,100000),("limit",20,20)):
        value=args.get(key,default)
        if type(value) is not int or value< (1 if key=="limit" else 0) or value>maximum:
            raise ValueError("Invalid browse page")
    if not args.get("provider"):
        raise ValueError("Missing provider")
    if args["kind"] in {"artist_albums","album_tracks","playlist_tracks"} and not args.get("parent_id"):
        raise ValueError("Missing parent")
    if args["kind"]=="folders" and not args.get("path"):
        raise ValueError("Missing browse path")
    return args

class BrowseRequest:
    def __init__(self,message_id,args):
        validate_browse(args)
        self.message_id=message_id
        self.kind=args["kind"]
        self.offset=args.get("offset",0)
        self.limit=args.get("limit",20)
        self.provider=args["provider"]
        self.items=[]
        self.seen=0
        self.created=time.monotonic()
        self.paged=self.kind in {"albums","artists","playlists"}
        if self.paged:
            self.command=f"music/{self.kind}/library_items"
            self.args={"provider":self.provider,"offset":self.offset,"limit":self.limit+1}
        elif self.kind=="folders":
            self.command="music/browse"; self.args={"path":args["path"]}
        else:
            commands={"artist_albums":"music/artists/artist_albums", "album_tracks":"music/albums/album_tracks", "playlist_tracks":"music/playlists/playlist_tracks"}
            self.command=commands[self.kind]
            self.args={"item_id":args["parent_id"],"provider_instance_id_or_domain":self.provider}
            if self.kind=="artist_albums" and args.get("source"):
                self.args["provider_filter"]=args["source"]

    def feed(self,result):
        if isinstance(result,dict): result=result.get("items")
        if not isinstance(result,list): raise ValueError("Unexpected browse result")
        start=0 if self.paged else self.offset
        for item in result:
            position=self.seen; self.seen+=1
            if position<start or position>=start+self.limit: continue
            if not isinstance(item,dict): continue
            kind=item.get("media_type") or {"albums":"album","artist_albums":"album","artists":"artist","playlists":"playlist"}.get(self.kind,"track")
            if not isinstance(kind,str) or kind not in {"album","artist","playlist","track","radio","folder","podcast_episode","audiobook"}: continue
            identity=item.get("item_id",""); provider=item.get("provider") or self.provider
            uri=item.get("path") if kind=="folder" else item.get("uri")
            if kind=="folder" and isinstance(uri,str): identity=sha256(uri.encode()).hexdigest()
            artists=item.get("artists") or []
            if not isinstance(artists,list): artists=[]
            subtitle=(artists[0].get("name","") if artists and isinstance(artists[0],dict) else item.get("owner", ""))
            valid=all(isinstance(v,str) and 0<len(v.encode("utf-8"))<=maximum for v,maximum in ((identity,128),(provider,95),(uri,512)))
            self.items.append({"id":identity if isinstance(identity,str) and len(identity.encode("utf-8"))<=128 else "",
                "provider":provider if isinstance(provider,str) and len(provider.encode("utf-8"))<=95 else "",
                "uri":uri if isinstance(uri,str) and len(uri.encode("utf-8"))<=512 else "",
                "name":str(item.get("name") or "Unnamed item").encode("utf-8")[:159].decode("utf-8","ignore"),"subtitle":str(subtitle).encode("utf-8")[:159].decode("utf-8","ignore"),
                "kind": "track" if kind in {"radio","podcast_episode","audiobook"} else kind,
                "available":valid and item.get("available") is not False})

    def response(self):
        return {"message_id":self.message_id,"result":{"items":self.items[:self.limit],
            "offset":self.offset,"has_more":self.seen>(self.limit if self.paged else self.offset+self.limit),"kind":self.kind}}


SEARCH_TYPES = {"tracks": "track", "albums": "album", "artists": "artist", "playlists": "playlist"}

def validate_search(args):
    if not isinstance(args, dict) or set(args) - {"query", "provider", "kind"}:
        raise ValueError("Invalid search arguments")
    query = args.get("query")
    provider = args.get("provider")
    if not isinstance(query, str) or not query.strip() or len(query.encode("utf-8")) > 192:
        raise ValueError("Invalid search query")
    if not isinstance(provider, str) or not provider or len(provider.encode("utf-8")) > 95:
        raise ValueError("Invalid search provider")
    if not isinstance(args.get("kind"),str) or args["kind"] not in SEARCH_TYPES:
        raise ValueError("Invalid search type")
    return args

class SearchRequest(BrowseRequest):
    """A single-provider, single-type search with a bounded normalized result."""
    def __init__(self, message_id, args):
        validate_search(args)
        self.message_id = message_id
        self.kind = args["kind"]
        self.offset = 0
        self.limit = 20
        self.provider = args["provider"]
        self.items = []
        self.seen = 0
        self.created = time.monotonic()
        self.paged = True
        self.library_fallback = False
        self.command = "music/search"
        self.args = {"search_query": args["query"].strip().casefold(), "providers": [self.provider],
                     "media_types": [SEARCH_TYPES[self.kind]], "limit": 21}

    def retry_older_server(self, error):
        # Only an explicit unsupported providers argument permits this bounded fallback.
        details=str(error.get("details") or error.get("error") or "").lower()
        if self.library_fallback or "providers" not in details or not any(word in details for word in ("unexpected", "unknown", "invalid argument")):
            return False
        self.library_fallback=True; self.items=[]; self.seen=0
        self.command=f"music/{self.kind}/library_items"
        self.args={"search":self.args["search_query"],"provider":self.provider,"limit":21,"offset":0}
        return True

    def response(self):
        response=super().response()
        response["result"]["scope"]="library" if self.library_fallback else "provider"
        return response

    def feed(self, result):
        if self.library_fallback:
            super().feed(result); return
        if not isinstance(result, dict) or not isinstance(result.get(self.kind), list):
            raise ValueError("Unexpected search result")
        super().feed(result[self.kind])
