from datetime import datetime
import sys
from google import genai



def connect():
# 1. The client automatically picks up the API key from the 
#    GEMINI_API_KEY environment variable.
    try:
        with open("env_variables","r") as file:
            API_KEY=file.readline().strip()
        client = genai.Client(api_key=API_KEY)
    except Exception:
        # Fallback: If environment variable isn't set, 
        # you can pass the key directly (less secure):
        # client = genai.Client(api_key="YOUR_API_KEY_HERE")
        print("API Key is not set in environment variable GEMINI_API_KEY. Please check setup.")
        exit()

    MODEL_NAME = "gemini-2.5-flash"
    # print(MODEL_NAME,client)
    return (MODEL_NAME, client)

def generate_report(commit_message, session_number):
    MODEL_NAME,client = connect()
    output_length = 20*len(commit_message) #we could use this to make the output proportional to how much was changed?
    user_prompt="Use the message that was provided:"
    user_prompt+=commit_message
    user_prompt+="and the current progress of the session:"
    with open("reports/report_"+str(session_number)+".txt","r") as file:
        current_report = file.read()
    user_prompt+=current_report
    user_prompt+="Do not acknowledge the input, only write a report. Keep your answer short. Write the answer in the first person, that is, use the word I a lot. The goal is to indicate what I have done, and what the goal of the next session is."
    user_prompt+="The rubric of the entire session is the following:"
    user_prompt+="""
    Level of Detail Described: First, did you provide enough detail in each entry for others (such as a typical ECE senior level student) to fully understand exactly what project work you performed?
    Does each session answer the six questions: What did you work on? How does this work build upon your team's previous related work? How did you do that work (tools, programs, test rigs, etc.)? What was the result and what did you learn from this work? How does your work relate to your team's project progress? What are the next steps? Do NOT include irrelevant information, puffery language, or other “fluff.”
    """
    user_prompt+="In this response, only generate the conclusion of the session. That is, connect the work done in this session and the commit message to determine what the next session's goal will be."
    response = client.models.generate_content(
        model=MODEL_NAME,
        contents=user_prompt
    )
    filename = "reports/report_"+str(session_number)+".txt"
    with open(filename, "a") as file:
        file.write(response.text)


with open("reports/session_number", "r") as file:
    last_line = file.readlines()[-1]
if ("end" not in last_line):
    last_session = int(last_line.split()[0])
    with open("reports/session_number","a") as file:
        file.write(" end")
    with open("reports/report_"+str(last_session)+".txt","a") as file:
        file.write("Ending Journaling Session at:"+ str(datetime.now())+"\n")
        print("Ending Journaling Session at:"+ str(datetime.now()))
    generate_report(sys.argv[1],last_session)
else:
    print("You cannot end a session you have not started.")
    