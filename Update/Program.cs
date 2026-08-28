using System;
using System.Diagnostics;
using System.IO;
using System.IO.Compression;
using System.Net;
using System.Net.Http;
using System.Security.Cryptography;
using System.Text.Json;
using System.Text.Json.Nodes;
using System.Threading;
using System.Threading.Tasks;


namespace Update
{
    internal class Program
    {
        static void Main(string[] args)
        {
            string[] launchArgs = new string[3] { " code:1622 ver:200 logip:", " logport:11000 chatip:", " chatport:8002 setting:\"file://data/features.xml=Regular, Japan\"" };
            const string updateURL = "https://github.com/ryuugana/kanan-mabipro/releases/latest/download/KananMabiPro.zip";
            const string fileName = "update.zip";

            Console.WriteLine("Waiting for Client.exe to close...");
            while(Process.GetProcessesByName("client.exe").Length > 0)
            {
                Thread.Sleep(1000);
            }

            Console.WriteLine("Client.exe closed, downloading Kanan update...");

            string fileHash = string.Empty;
            string updateHash = GetUpdateHash().GetAwaiter().GetResult();
            while (fileHash != updateHash)
            {
                if(fileHash != string.Empty)
                {
                    Console.WriteLine("Update failed to download! Retrying...");
                    Thread.Sleep(5000);
                }

                DownloadFile(updateURL, fileName);
                fileHash = GetFileSHA256Hash(fileName);

                Console.WriteLine("Verifying downloaded update:");
                Console.WriteLine($"Comparing downloaded file hash {fileHash} with known update hash {updateHash}");
            }

            Console.WriteLine("Update downloaded successfully");

            Console.Write("Extracting update: ");

            try
            {
                UnzipFile(fileName);
                Console.WriteLine("Extraction complete");
            }
            catch (Exception ex)
            {
                Console.WriteLine("Extraction failed");
                Console.WriteLine(ex.ToString());
            }

            Console.Write("Getting MabiPro login IP Address: ");

            // Grab patch.txt and assign values
            HttpWebRequest request = (HttpWebRequest)WebRequest.Create("https://mabi.pro/patch/p.txt");
            string loginIpAddr = "15.204.20.234";
            try
            {
                request.Timeout = 1000;
                using (HttpWebResponse response = (HttpWebResponse)request.GetResponse())
                using (Stream stream = response.GetResponseStream())
                using (StreamReader reader = new StreamReader(stream))
                {
                    string html = reader.ReadLine();
                    string[] split = html.Split(' ');
                    loginIpAddr = split[1];
                }

                Console.WriteLine("Obtained MabiPro login IP - " + loginIpAddr);
            }
            catch (Exception)
            {
                Console.WriteLine("Failed to obtain MabiPro login IP, using default - " + loginIpAddr);
                Console.WriteLine("Warning: The default IP may not be correct, please manually relaunch if login fails.");
            }

            Console.WriteLine("Starting Client.exe");

            // Sleep to show results to make any errors visible before the client launches
            Thread.Sleep(1000);

            try
            {
                Process.Start("Client.exe", launchArgs[0] + loginIpAddr + launchArgs[1] + loginIpAddr + launchArgs[2]);
            }
            catch (Exception e)
            {
                Console.WriteLine("Unable to start client.exe!");
                Console.WriteLine(e.ToString());
            }

            Console.WriteLine("Waiting a bit before closing to show results...");
            Thread.Sleep(20000);
        }

        static async Task<string> GetUpdateHash()
        {
            string owner = "ryuugana";
            string repo = "kanan-mabipro";
            var client = new HttpClient
            {
                BaseAddress = new Uri("https://api.github.com/")
            };
            client.DefaultRequestHeaders.UserAgent.ParseAdd("UpdateKanan");

            // 1. Get the latest release → tag_name
            var release = await client.GetAsync($"repos/{owner}/{repo}/releases/latest");

            string json = await release.Content.ReadAsStringAsync();

            var doc = JsonDocument.Parse(json);
            JsonNode assetJson = JsonNode.Parse(doc.RootElement.GetProperty("assets").GetRawText());

            // Access the first element (index 0)
            JsonNode firstElement = assetJson?[0];
            string hash = string.Empty;

            // Extract specific values
            if (firstElement != null)
            {
                hash = firstElement["digest"].GetValue<string>();
            }

            if(hash.Length > 7)
            {
                return hash.Substring(7, hash.Length - 7);
            }

            return hash;
        }

        public static string GetFileSHA256Hash(string filePath)
        {
            using (var sha256 = SHA256.Create())
            using (var stream = File.OpenRead(filePath))
            {
                byte[] hashBytes = sha256.ComputeHash(stream);
                // Convert byte array to hex string
                return BitConverter.ToString(hashBytes).Replace("-", "").ToLowerInvariant();
            }
        }

        static void DownloadFile(string URL, string fileName)
        {
            using (var newPatch = new WebClient())
            {
                newPatch.DownloadFile(new Uri(URL), @".\" + fileName);
            }
        }

        static void UnzipFile(string fileName)
        {
            // Extract new patch
            using (ZipArchive archive = ZipFile.Open(@".\" + fileName, ZipArchiveMode.Read))
            {
                // Extract to directory above; updater will be extracted to a folder within Mabi
                archive.ExtractToDirectory(@"..\", true);
            }
            File.Delete(@".\" + fileName);
        }
    }

    // Inherit ExtractToDirectory from ZipArchiveExtensions 
    public static class ZipArchiveExtensions
    {
        // This function overwrites files in the directory when setting the bool argument to true
        public static void ExtractToDirectory(this ZipArchive archive, string destinationDirectoryName, bool overwrite)
        {
            if (!overwrite)
            {
                archive.ExtractToDirectory(destinationDirectoryName);
                return;
            }
            foreach (ZipArchiveEntry file in archive.Entries)
            {
                string completeFileName = System.IO.Path.Combine(destinationDirectoryName, file.FullName);
                string directory = System.IO.Path.GetDirectoryName(completeFileName);

                if (!Directory.Exists(directory))
                    Directory.CreateDirectory(directory);

                if (file.Name != "")
                    file.ExtractToFile(completeFileName, true);
            }
        }
    }
}
