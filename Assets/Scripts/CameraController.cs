using System.Collections;
using System.Collections.Generic;
using UnityEngine;

public class CameraController : MonoBehaviour
{

    private GameObject player;

    private Vector3 cameraOffset = new Vector3(0, 0, -10);
    	
	void Start ()
    {
        player = GameObject.Find("Player");
	}
	
	// Update is called once per frame
	void Update ()
    {
        transform.position = player.transform.position;
        transform.position += cameraOffset;
	}
}
