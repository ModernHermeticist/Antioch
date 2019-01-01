using System.Collections;
using System.Collections.Generic;
using UnityEngine;

public class Player : MonoBehaviour 
{

	public float speed = 1.5f;


	void Start () 
	{
		
	}
	
	// Update is called once per frame
	void Update () 
	{
		Vector3 velocity = new Vector3(0,0,0);

		if (Input.GetKey("w")) 
		{
			velocity.y += speed * Time.deltaTime;
		}

		if (Input.GetKey("s"))
		{
			velocity.y -= speed * Time.deltaTime;
		}

		if (Input.GetKey("d"))
		{
			velocity.x += speed * Time.deltaTime;
		}

		if (Input.GetKey("a"))
		{
			velocity.x -= speed * Time.deltaTime;
		}

		if (velocity.magnitude > 0)
		{
			velocity = velocity.normalized * speed;
		}
		transform.position += velocity * Time.deltaTime;
	}

	void OnCollisionEnter2D(Collision2D col)
	{
		Debug.Log("OnCollisionEnter2D");
	}

	void OnCollisionExit2D(Collision2D col)
	{
		Debug.Log("OnCollisionExit2D");
	}
}
