param(
    [string]$OutputDirectory = "",
    [string]$ContactSheetPath = ""
)
# Rebuild the committed PNG, ICO and ICNS files from helpdesign.svg.
# Requires Windows PowerShell 5.1 (built-in System.Drawing); no downloaded tools.
# Run: powershell.exe -NoProfile -File scripts/generate-app-icons.ps1
# The small SVG renderer supports the absolute M/L/H/V/Q/Z commands used here.
$ErrorActionPreference = "Stop"
$projectRoot = Split-Path $PSScriptRoot -Parent
if (-not $OutputDirectory) { $OutputDirectory = Join-Path $projectRoot "assets/icons" }
if (-not $ContactSheetPath) { $ContactSheetPath = Join-Path $projectRoot "artifacts/icons/helpdesign-contact-sheet.png" }
$ContactSheetPath = [IO.Path]::GetFullPath($ContactSheetPath)
[IO.Directory]::CreateDirectory((Split-Path $ContactSheetPath -Parent)) | Out-Null
$source = Join-Path $projectRoot "assets/icons/helpdesign.svg"
[IO.Directory]::CreateDirectory($OutputDirectory) | Out-Null
Add-Type -AssemblyName System.Drawing
Add-Type -ReferencedAssemblies System.Drawing,System.Xml -TypeDefinition @'
using System;
using System.Collections.Generic;
using System.Drawing;
using System.Drawing.Drawing2D;
using System.Drawing.Imaging;
using System.Globalization;
using System.IO;
using System.Text;
using System.Text.RegularExpressions;
using System.Xml;

public static class HelpDesignIconGenerator {
    static float Number(string s) { return float.Parse(s, CultureInfo.InvariantCulture); }
    static float Attr(XmlNode n, string name) { return Number(n.Attributes[name].Value); }
    static GraphicsPath RoundRect(float x, float y, float w, float h, float r) {
        var p = new GraphicsPath();
        p.AddArc(x, y, 2*r, 2*r, 180, 90);
        p.AddArc(x+w-2*r, y, 2*r, 2*r, 270, 90);
        p.AddArc(x+w-2*r, y+h-2*r, 2*r, 2*r, 0, 90);
        p.AddArc(x, y+h-2*r, 2*r, 2*r, 90, 90);
        p.CloseFigure(); return p;
    }
    static GraphicsPath SvgPath(string data) {
        var tokens = Regex.Matches(data, @"[MLHVQZ]|-?\d+(?:\.\d+)?");
        int i=0; float x=0, y=0; var p = new GraphicsPath();
        Func<float> next = () => Number(tokens[i++].Value);
        while(i<tokens.Count) {
            string command=tokens[i++].Value;
            if(command=="M") { x=next(); y=next(); p.StartFigure(); }
            else if(command=="L") { float a=next(), b=next(); p.AddLine(x,y,a,b); x=a; y=b; }
            else if(command=="H") { float a=next(); p.AddLine(x,y,a,y); x=a; }
            else if(command=="V") { float b=next(); p.AddLine(x,y,x,b); y=b; }
            else if(command=="Q") {
                float a=next(), b=next(), c=next(), d=next();
                p.AddBezier(x,y,x+(a-x)*2/3,y+(b-y)*2/3,c+(a-c)*2/3,d+(b-d)*2/3,c,d);
                x=c; y=d;
            } else if(command=="Z") { p.CloseFigure(); }
            else throw new InvalidDataException("Unsupported SVG path command: "+command);
        }
        return p;
    }
    static Bitmap Render(XmlDocument svg, int size) {
        // Supersampling preserves clean edges and transparent corners at small sizes.
        int scale = size < 256 ? 4 : 2;
        using(var large = new Bitmap(size*scale, size*scale, PixelFormat.Format32bppArgb)) {
            using(var g = Graphics.FromImage(large)) {
                g.Clear(Color.Transparent);
                g.SmoothingMode = SmoothingMode.AntiAlias;
                g.PixelOffsetMode = PixelOffsetMode.HighQuality;
                g.ScaleTransform(size*scale/512f, size*scale/512f);
                foreach(XmlNode n in svg.DocumentElement.ChildNodes) {
                    if(n.LocalName=="rect") {
                        float x=Attr(n,"x"), y=Attr(n,"y"), w=Attr(n,"width"), h=Attr(n,"height"), r=Attr(n,"rx");
                        var stops=svg.GetElementsByTagName("stop");
                        using(var fill=new LinearGradientBrush(new PointF(x,y),new PointF(x,y+h),
                            ColorTranslator.FromHtml(stops[0].Attributes["stop-color"].Value),
                            ColorTranslator.FromHtml(stops[1].Attributes["stop-color"].Value)))
                        using(var path=RoundRect(x,y,w,h,r)) { g.FillPath(fill,path); }
                    } else if(n.LocalName=="path") {
                        using(var path=SvgPath(n.Attributes["d"].Value)) {
                            if(n.Attributes["fill"].Value!="none") {
                                using(var fill=new SolidBrush(ColorTranslator.FromHtml(n.Attributes["fill"].Value)))
                                    g.FillPath(fill,path);
                            }
                            if(n.Attributes["stroke"]!=null) {
                                using(var pen=new Pen(ColorTranslator.FromHtml(n.Attributes["stroke"].Value),Attr(n,"stroke-width"))) {
                                    pen.StartCap=LineCap.Round; pen.EndCap=LineCap.Round; pen.LineJoin=LineJoin.Round;
                                    g.DrawPath(pen,path);
                                }
                            }
                        }
                    }
                }
            }
            var result = new Bitmap(size,size,PixelFormat.Format32bppArgb);
            using(var g=Graphics.FromImage(result)) {
                g.CompositingMode=CompositingMode.SourceCopy;
                g.InterpolationMode=InterpolationMode.HighQualityBicubic;
                g.PixelOffsetMode=PixelOffsetMode.HighQuality;
                g.DrawImage(large,new Rectangle(0,0,size,size),0,0,large.Width,large.Height,GraphicsUnit.Pixel);
            }
            return result;
        }
    }
    static void BigEndian(BinaryWriter writer, int value) {
        writer.Write(new byte[] {(byte)(value>>24),(byte)(value>>16),(byte)(value>>8),(byte)value});
    }
    public static void Generate(string source, string output, string contactSheetPath) {
        var svg=new XmlDocument(); svg.Load(source);
        var pngs=new Dictionary<int,byte[]>();
        int[] sizes={16,20,24,32,48,64,128,256,512,1024};
        foreach(int size in sizes) {
            using(var bitmap=Render(svg,size)) {
                string file=Path.Combine(output,"helpdesign-"+size+".png");
                bitmap.Save(file,ImageFormat.Png); pngs[size]=File.ReadAllBytes(file);
            }
        }
        // Windows 10+ accepts PNG-compressed, 32-bit RGBA entries at every size.
        int[] icoSizes={16,20,24,32,48,64,128,256};
        using(var writer=new BinaryWriter(File.Create(Path.Combine(output,"helpdesign.ico")))) {
            writer.Write((ushort)0); writer.Write((ushort)1); writer.Write((ushort)icoSizes.Length);
            int offset=6+16*icoSizes.Length;
            foreach(int size in icoSizes) {
                writer.Write((byte)(size==256 ? 0 : size)); writer.Write((byte)(size==256 ? 0 : size));
                writer.Write((byte)0); writer.Write((byte)0); writer.Write((ushort)1); writer.Write((ushort)32);
                writer.Write(pngs[size].Length); writer.Write(offset); offset+=pngs[size].Length;
            }
            foreach(int size in icoSizes) writer.Write(pngs[size]);
        }
        // Modern ICNS PNG chunks, including Retina representations (32 -> 16@2x).
        string[] types={"icp4","icp5","icp6","ic07","ic08","ic09","ic10","ic11","ic12","ic13","ic14"};
        int[] icnsSizes={16,32,64,128,256,512,1024,32,64,256,512};
        int length=8; foreach(int size in icnsSizes) length+=8+pngs[size].Length;
        using(var writer=new BinaryWriter(File.Create(Path.Combine(output,"helpdesign.icns")))) {
            writer.Write(Encoding.ASCII.GetBytes("icns")); BigEndian(writer,length);
            for(int i=0;i<types.Length;i++) {
                writer.Write(Encoding.ASCII.GetBytes(types[i])); BigEndian(writer,8+pngs[icnsSizes[i]].Length);
                writer.Write(pngs[icnsSizes[i]]);
            }
        }
        // Review native-size 16/32 icons on light/dark surfaces, alongside the artwork.
        using(var sheet=new Bitmap(920,520))
        using(var g=Graphics.FromImage(sheet))
        using(var text=new SolidBrush(Color.FromArgb(55,65,81)))
        using(var dark=new SolidBrush(Color.FromArgb(30,34,42)))
        using(var font=new Font("Segoe UI",12))
        using(var title=new Font("Segoe UI",20,FontStyle.Bold)) {
            g.Clear(Color.FromArgb(244,246,249));
            using(var large=Render(svg,384)) g.DrawImageUnscaled(large,30,58);
            g.DrawString("HelpDesign",title,text,56,22);
            g.DrawString("Native sizes",font,text,476,54);
            int[] review={16,20,24,32,48,64}; int x=476;
            foreach(int size in review) {
                g.FillRectangle(Brushes.White,x-8,92,Math.Max(size+16,40),96);
                g.FillRectangle(dark,x-8,194,Math.Max(size+16,40),96);
                using(var bitmap=Render(svg,size)) { g.DrawImageUnscaled(bitmap,x,112); g.DrawImageUnscaled(bitmap,x,214); }
                g.DrawString(size+"px",font,text,x-5,304); x+=Math.Max(size+30,54);
            }
            using(var bitmap=Render(svg,16)) {
                g.InterpolationMode=InterpolationMode.NearestNeighbor; g.PixelOffsetMode=PixelOffsetMode.Half;
                g.DrawImage(bitmap,new Rectangle(486,354,96,96),0,0,16,16,GraphicsUnit.Pixel);
            }
            using(var bitmap=Render(svg,32)) g.DrawImage(bitmap,new Rectangle(664,354,96,96),0,0,32,32,GraphicsUnit.Pixel);
            g.DrawString("16px x6",font,text,486,460); g.DrawString("32px x3",font,text,664,460);
            sheet.Save(contactSheetPath,ImageFormat.Png);
        }
    }
}
'@
[HelpDesignIconGenerator]::Generate($source, [IO.Path]::GetFullPath($OutputDirectory), $ContactSheetPath)
Write-Host "Generated HelpDesign icons in $OutputDirectory"
